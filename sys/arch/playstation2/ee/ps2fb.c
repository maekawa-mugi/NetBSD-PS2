/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * GS as a wsdisplay(4) framebuffer. No SBIOS, RTE, IOP, DMA or VU use.
 *
 * Pixels live in a physically contiguous shadow buffer in main memory and
 * are drawn by rasops32/vcons. Changed rectangles are pushed to GS local
 * memory through GIF PATH3 PIO host->local transfers (EE User's Manual
 * ch.7, GS User's Manual ch.4/5/7), and scrolling uses a GS local->local
 * copy. The same buffer is what a client maps in WSDISPLAYIO_MODE_DUMBFB:
 * the GS cannot be read or mapped by the EE, so while a client owns the
 * display the buffer is copied to the GS periodically (PS2FBIO_SRATE).
 * PCRTC timings follow gs_console.c.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/errno.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_page.h>

#include <dev/cons.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsdisplay_vconsvar.h>
#include <dev/rasops/rasops.h>

#include <mips/cpuregs.h>
#include <machine/autoconf.h>
#include <machine/intr.h>
#include <machine/ps2fbio.h>
#include <playstation2/ee/gsreg.h>

#define GIF_STAT (*(volatile uint32_t *)MIPS_PHYS_TO_KSEG1(0x10003020))
#define GSWRITE(reg, value) (*(volatile uint64_t *)(reg) = (value))

#define TEXT_WIDTH	640
#define TEXT_HEIGHT	480
#define DEFAULT_RATE	20	/* Hz, DUMBFB periodic upload */

struct ps2fb_softc {
	device_t sc_dev;
	struct vcons_data vd;
	struct vcons_screen console_screen;
	struct wsscreen_descr sd;
	const struct wsscreen_descr *sdp[1];
	struct wsscreen_list sl;
	struct wsdisplay_accessops ops;
	struct wsdisplay_emulops orig;	/* rasops drawing, before upload hooks */

	uint32_t *shadow;
	paddr_t shadow_pa;
	size_t shadow_size;

	u_int width, height;		/* mode the GS shows now */
	int mode;			/* WSDISPLAYIO_MODE_* */
	bool video_on;
	bool gs_ok;
	callout_t flush_ch;
	u_int rate;
};

static struct ps2fb_softc *ps2fb_sc;

extern void (*ps2_gs_putc_hook)(int);

static int ps2fb_match(device_t, cfdata_t, void *);
static void ps2fb_attach(device_t, device_t, void *);
CFATTACH_DECL_NEW(ps2fb, sizeof(struct ps2fb_softc), ps2fb_match,
    ps2fb_attach, NULL, NULL);

/* ---- GS transfer layer -------------------------------------------- */

struct gs_qword { uint64_t lo, hi; } __aligned(16);

static bool
gs_qw(struct ps2fb_softc *sc, uint64_t lo, uint64_t hi)
{
	struct gs_qword q = { lo, hi };
	unsigned timeout = 1000000;

	while (((GIF_STAT >> 24) & 31) >= 16)
		if (--timeout == 0) {
			sc->gs_ok = false;
			return false;
		}
	__asm volatile("lq $8,0(%0); sq $8,0(%1); sync.p"
	    :: "r"(&q), "r"(MIPS_PHYS_TO_KSEG1(0x10006000))
	    : "$8", "memory");
	return true;
}

/* BITBLTBUF/TRXPOS/TRXREG/TRXDIR via PACKED A+D. */
static bool
gs_trx(struct ps2fb_softc *sc, u_int x, u_int y, u_int w, u_int h, u_int sy,
    bool copy)
{
	uint64_t fbw = sc->width / 64;

	return gs_qw(sc, 4 | 0x8000 | (1ULL << 60), 0xe) &&
	    gs_qw(sc, (fbw << 48) | (copy ? fbw << 16 : 0), 0x50) &&
	    gs_qw(sc, ((uint64_t)y << 48) | ((uint64_t)x << 32) |
	        ((uint64_t)sy << 16), 0x51) &&
	    gs_qw(sc, ((uint64_t)h << 32) | w, 0x52) &&
	    gs_qw(sc, copy ? 2 : 0, 0x53);
}

/* One IMAGE transfer: GIFtag NLOOP is 15 bits, so callers stay under it. */
static void gs_upload_band(struct ps2fb_softc *, u_int, u_int, u_int, u_int);

/* Push a rectangle of the shadow buffer to GS local memory. */
static void
gs_upload(struct ps2fb_softc *sc, u_int x, u_int y, u_int w, u_int h)
{
	u_int band;

	if (x >= sc->width || y >= sc->height)
		return;
	if (x + w > sc->width)
		w = sc->width - x;
	if (y + h > sc->height)
		h = sc->height - y;
	if (w == 0 || h == 0)
		return;
	/* A stuck FIFO disables uploads only until the next attempt. */
	sc->gs_ok = true;
	band = MAX(1, (32767 * 4) / w);
	while (h > 0) {
		u_int n = MIN(h, band);

		gs_upload_band(sc, x, y, w, n);
		y += n;
		h -= n;
	}
}

static void
gs_upload_band(struct ps2fb_softc *sc, u_int x, u_int y, u_int w, u_int h)
{
	u_int nqw, i, j, n;
	uint32_t pix[4];
	int s;

	if (!sc->gs_ok)
		return;
	nqw = (w * h + 3) / 4;
	s = _intr_suspend();
	if (gs_trx(sc, x, y, w, h, 0, false) &&
	    gs_qw(sc, nqw | 0x8000 | (2ULL << 58), 0)) {
		n = 0;
		for (j = 0; j < h; j++) {
			const uint32_t *p = sc->shadow + (y + j) * sc->width + x;
			for (i = 0; i < w; i++) {
				pix[n++] = p[i];
				if (n == 4) {
					if (!gs_qw(sc,
					    ((uint64_t)pix[1] << 32) | pix[0],
					    ((uint64_t)pix[3] << 32) | pix[2]))
						goto out;
					n = 0;
				}
			}
		}
		if (n != 0) {
			for (i = n; i < 4; i++)
				pix[i] = 0;
			gs_qw(sc, ((uint64_t)pix[1] << 32) | pix[0],
			    ((uint64_t)pix[3] << 32) | pix[2]);
		}
	}
out:
	_intr_resume(s);
}

static void
gs_copy_up(struct ps2fb_softc *sc, u_int sy, u_int dy, u_int h)
{
	int s;

	if (!sc->gs_ok)
		return;
	s = _intr_suspend();
	(void)gs_trx(sc, 0, dy, sc->width, h, sy, true);
	_intr_resume(s);
}

static void
gs_program_display(struct ps2fb_softc *sc)
{
	u_int magh = 1280 / sc->width - 1;
	u_int magv = 480 / sc->height - 1;

	GSWRITE(GS_S_DISPLAY2_REG, DISPLAY(479, 1279, magv, magh, 34, 276));
	GSWRITE(GS_S_DISPFB2_REG, (uint64_t)(sc->width / 64) << 9);
	GSWRITE(GS_S_PMODE_REG, sc->video_on ? 0x8022 : 0);
}

/* ---- rasops hooks: draw into the shadow, then upload the rectangle -- */

static inline struct ps2fb_softc *
cell_rect(void *cookie, int row, int col, int nrows, int ncols,
    u_int *x, u_int *y, u_int *w, u_int *h)
{
	struct rasops_info *ri = cookie;

	*x = ri->ri_xorigin + col * ri->ri_font->fontwidth;
	*y = ri->ri_yorigin + row * ri->ri_font->fontheight;
	*w = ncols * ri->ri_font->fontwidth;
	*h = nrows * ri->ri_font->fontheight;
	return ps2fb_sc;
}

static void
ps2fb_putchar(void *c, int row, int col, u_int ch, long attr)
{
	u_int x, y, w, h;
	struct ps2fb_softc *sc = cell_rect(c, row, col, 1, 1, &x, &y, &w, &h);

	sc->orig.putchar(c, row, col, ch, attr);
	gs_upload(sc, x, y, w, h);
}

static void
ps2fb_cursor(void *c, int on, int row, int col)
{
	u_int x, y, w, h;
	struct ps2fb_softc *sc = cell_rect(c, row, col, 1, 1, &x, &y, &w, &h);

	sc->orig.cursor(c, on, row, col);
	gs_upload(sc, x, y, w, h);
}

static void
ps2fb_copycols(void *c, int row, int src, int dst, int ncols)
{
	u_int x, y, w, h;
	struct ps2fb_softc *sc = cell_rect(c, row, dst, 1, ncols, &x, &y, &w, &h);

	sc->orig.copycols(c, row, src, dst, ncols);
	gs_upload(sc, x, y, w, h);
}

static void
ps2fb_erasecols(void *c, int row, int col, int ncols, long attr)
{
	u_int x, y, w, h;
	struct ps2fb_softc *sc = cell_rect(c, row, col, 1, ncols, &x, &y, &w, &h);

	sc->orig.erasecols(c, row, col, ncols, attr);
	gs_upload(sc, x, y, w, h);
}

static void
ps2fb_copyrows(void *c, int src, int dst, int nrows)
{
	struct rasops_info *ri = c;
	struct ps2fb_softc *sc = ps2fb_sc;
	u_int x, y, w, h;

	sc->orig.copyrows(c, src, dst, nrows);
	if (dst < src && ri->ri_xorigin == 0 &&
	    ri->ri_width == sc->width) {
		/* Upward scroll: a top-to-bottom GS copy handles the overlap. */
		gs_copy_up(sc, ri->ri_yorigin + src * ri->ri_font->fontheight,
		    ri->ri_yorigin + dst * ri->ri_font->fontheight,
		    nrows * ri->ri_font->fontheight);
		return;
	}
	cell_rect(c, dst, 0, nrows, ri->ri_cols, &x, &y, &w, &h);
	gs_upload(sc, 0, y, sc->width, h);
}

static void
ps2fb_eraserows(void *c, int row, int nrows, long attr)
{
	struct rasops_info *ri = c;
	struct ps2fb_softc *sc = ps2fb_sc;
	u_int x, y, w, h;

	sc->orig.eraserows(c, row, nrows, attr);
	cell_rect(c, row, 0, nrows, ri->ri_cols, &x, &y, &w, &h);
	gs_upload(sc, 0, y, sc->width, h);
}

/* ---- wsdisplay glue ------------------------------------------------ */

static void
ps2fb_flush_all(struct ps2fb_softc *sc)
{
	gs_upload(sc, 0, 0, sc->width, sc->height);
}

static void
ps2fb_flush_tick(void *arg)
{
	struct ps2fb_softc *sc = arg;

	if (sc->mode != WSDISPLAYIO_MODE_EMUL && sc->rate != 0) {
		ps2fb_flush_all(sc);
		callout_schedule(&sc->flush_ch, MAX(1, hz / sc->rate));
	}
}

static void
ps2fb_set_mode(struct ps2fb_softc *sc, u_int w, u_int h)
{
	sc->width = w;
	sc->height = h;
	gs_program_display(sc);
}

static const struct ps2fb_mode ps2fb_modes[] = {
	{ 640, 480, 32 },
	{ 320, 240, 32 },
};

static void
ps2fb_init_screen(void *cookie, struct vcons_screen *scr, int existing,
    long *defattr)
{
	struct ps2fb_softc *sc = cookie;
	struct rasops_info *ri = &scr->scr_ri;

	ri->ri_depth = 32;
	ri->ri_width = TEXT_WIDTH;
	ri->ri_height = TEXT_HEIGHT;
	ri->ri_stride = TEXT_WIDTH * 4;
	ri->ri_bits = (char *)sc->shadow;
	/* PSMCT32 holds R in the low byte; alpha comes from PMODE.ALP. */
	ri->ri_rnum = ri->ri_gnum = ri->ri_bnum = 8;
	ri->ri_rpos = 0;
	ri->ri_gpos = 8;
	ri->ri_bpos = 16;
	ri->ri_flg = RI_CENTER | RI_FULLCLEAR;
	if (existing)
		ri->ri_flg |= RI_CLEAR;
	scr->scr_flags |= VCONS_LOADFONT;

	rasops_init(ri, TEXT_HEIGHT / 16, TEXT_WIDTH / 8);
	ri->ri_caps = WSSCREEN_WSCOLORS | WSSCREEN_HILIT |
	    WSSCREEN_UNDERLINE | WSSCREEN_REVERSE;
	rasops_reconfig(ri, TEXT_HEIGHT / ri->ri_font->fontheight,
	    TEXT_WIDTH / ri->ri_font->fontwidth);
	ri->ri_hw = scr;

	/* Same code for every screen; vcons wraps these once more. */
	sc->orig = ri->ri_ops;
	ri->ri_ops.putchar = ps2fb_putchar;
	ri->ri_ops.cursor = ps2fb_cursor;
	ri->ri_ops.copycols = ps2fb_copycols;
	ri->ri_ops.erasecols = ps2fb_erasecols;
	ri->ri_ops.copyrows = ps2fb_copyrows;
	ri->ri_ops.eraserows = ps2fb_eraserows;
}

static int
ps2fb_fbinfo(struct ps2fb_softc *sc, struct wsdisplayio_fbinfo *fbi)
{
	fbi->fbi_width = sc->width;
	fbi->fbi_height = sc->height;
	fbi->fbi_stride = sc->width * 4;
	fbi->fbi_bitsperpixel = 32;
	fbi->fbi_pixeltype = WSFB_RGB;
	fbi->fbi_subtype.fbi_rgbmasks.red_offset = 0;
	fbi->fbi_subtype.fbi_rgbmasks.red_size = 8;
	fbi->fbi_subtype.fbi_rgbmasks.green_offset = 8;
	fbi->fbi_subtype.fbi_rgbmasks.green_size = 8;
	fbi->fbi_subtype.fbi_rgbmasks.blue_offset = 16;
	fbi->fbi_subtype.fbi_rgbmasks.blue_size = 8;
	fbi->fbi_subtype.fbi_rgbmasks.alpha_offset = 0;
	fbi->fbi_subtype.fbi_rgbmasks.alpha_size = 0;
	fbi->fbi_flags = 0;
	fbi->fbi_fbsize = sc->width * sc->height * 4;
	fbi->fbi_fboffset = 0;
	return 0;
}

static int
ps2fb_ioctl(void *v, void *vs, u_long cmd, void *data, int flag,
    struct lwp *l)
{
	struct vcons_data *vd = v;
	struct ps2fb_softc *sc = vd->cookie;
	struct vcons_screen *ms = vd->active;
	struct wsdisplay_fbinfo *wdf;
	struct ps2fb_mode *pm;
	u_int i, v1;
	int s, nm;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = data;
		wdf->height = sc->height;
		wdf->width = sc->width;
		wdf->depth = 32;
		wdf->cmsize = 0;
		return 0;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->width * 4;
		return 0;
	case WSDISPLAYIO_GET_FBINFO:
		return ps2fb_fbinfo(sc, data);
	case WSDISPLAYIO_GVIDEO:
		*(u_int *)data = sc->video_on ? WSDISPLAYIO_VIDEO_ON :
		    WSDISPLAYIO_VIDEO_OFF;
		return 0;
	case WSDISPLAYIO_SVIDEO:
		sc->video_on = *(u_int *)data != WSDISPLAYIO_VIDEO_OFF;
		GSWRITE(GS_S_PMODE_REG, sc->video_on ? 0x8022 : 0);
		return 0;
	case WSDISPLAYIO_SMODE:
		nm = *(int *)data;
		if (nm == sc->mode)
			return 0;
		s = splhigh();
		sc->mode = nm;
		splx(s);
		if (nm == WSDISPLAYIO_MODE_EMUL) {
			callout_stop(&sc->flush_ch);
			if (sc->width != TEXT_WIDTH || sc->height != TEXT_HEIGHT)
				ps2fb_set_mode(sc, TEXT_WIDTH, TEXT_HEIGHT);
			if (ms != NULL)
				vcons_redraw_screen(ms);
		} else {
			ps2fb_flush_all(sc);
			if (sc->rate != 0)
				callout_schedule(&sc->flush_ch,
				    MAX(1, hz / sc->rate));
		}
		return 0;
	case PS2FBIO_GMODE:
		pm = data;
		i = pm->width;
		if (i >= __arraycount(ps2fb_modes))
			return EINVAL;
		*pm = ps2fb_modes[i];
		return 0;
	case PS2FBIO_SMODE:
		pm = data;
		if (sc->mode == WSDISPLAYIO_MODE_EMUL)
			return EBUSY;
		for (i = 0; i < __arraycount(ps2fb_modes); i++)
			if (ps2fb_modes[i].width == pm->width &&
			    ps2fb_modes[i].height == pm->height &&
			    (pm->depth == 0 || pm->depth == 32)) {
				ps2fb_set_mode(sc, pm->width, pm->height);
				memset(sc->shadow, 0, sc->shadow_size);
				ps2fb_flush_all(sc);
				return 0;
			}
		return EINVAL;
	case PS2FBIO_SRATE:
		v1 = *(u_int *)data;
		if (v1 > hz)
			return EINVAL;
		sc->rate = v1;
		callout_stop(&sc->flush_ch);
		if (v1 != 0 && sc->mode != WSDISPLAYIO_MODE_EMUL)
			callout_schedule(&sc->flush_ch, MAX(1, hz / v1));
		return 0;
	case PS2FBIO_FLUSH:
		ps2fb_flush_all(sc);
		return 0;
	}
	return EPASSTHROUGH;
}

static paddr_t
ps2fb_mmap(void *v, void *vs, off_t offset, int prot)
{
	struct vcons_data *vd = v;
	struct ps2fb_softc *sc = vd->cookie;

	if (offset < 0 || (size_t)offset >= sc->shadow_size ||
	    sc->mode == WSDISPLAYIO_MODE_EMUL)
		return -1;
	return mips_btop(sc->shadow_pa + offset);
}

static void
ps2fb_pollc(void *v, int on)
{
	struct vcons_data *vd = v;

	if (on)
		vcons_enable_polling(vd);
	else
		vcons_disable_polling(vd);
}

/* Kernel console text (printf, panic) follows the wsdisplay console screen. */
static void
ps2fb_kputc(int c)
{
	wsdisplay_cnputc(0, c);
}

static int
ps2fb_match(device_t parent, cfdata_t cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	return ps2fb_sc == NULL && strcmp(ma->ma_name, "ps2fb") == 0;
}

static void
ps2fb_attach(device_t parent, device_t self, void *aux)
{
	struct ps2fb_softc *sc = device_private(self);
	struct wsemuldisplaydev_attach_args aa;
	struct rasops_info *ri;
	struct consdev *saved_cn;
	struct pglist pgl;
	struct vm_page *pg;
	long defattr;

	sc->sc_dev = self;
	sc->width = TEXT_WIDTH;
	sc->height = TEXT_HEIGHT;
	sc->mode = WSDISPLAYIO_MODE_EMUL;
	sc->video_on = true;
	sc->gs_ok = true;
	sc->rate = DEFAULT_RATE;
	callout_init(&sc->flush_ch, 0);
	callout_setfunc(&sc->flush_ch, ps2fb_flush_tick, sc);

	sc->shadow_size = round_page(TEXT_WIDTH * TEXT_HEIGHT * 4);
	TAILQ_INIT(&pgl);
	if (uvm_pglistalloc(sc->shadow_size, 0, ptoa((paddr_t)physmem) - 1, PAGE_SIZE, 0,
	    &pgl, 1, 1) != 0) {
		aprint_error(": cannot allocate framebuffer\n");
		return;
	}
	pg = TAILQ_FIRST(&pgl);
	sc->shadow_pa = VM_PAGE_TO_PHYS(pg);
	sc->shadow = (uint32_t *)MIPS_PHYS_TO_KSEG0(sc->shadow_pa);
	memset(sc->shadow, 0, sc->shadow_size);
	ps2fb_sc = sc;

	aprint_normal(": GS %ux%u 32bpp PSMCT32, %zu KiB shadow framebuffer\n",
	    sc->width, sc->height, sc->shadow_size >> 10);

	sc->sd = (struct wsscreen_descr){
		"default", 0, 0, NULL, 8, 16,
		WSSCREEN_WSCOLORS | WSSCREEN_HILIT | WSSCREEN_UNDERLINE |
		    WSSCREEN_REVERSE,
		NULL
	};
	sc->sdp[0] = &sc->sd;
	sc->sl = (struct wsscreen_list){ 1, sc->sdp };
	sc->ops.ioctl = ps2fb_ioctl;
	sc->ops.mmap = ps2fb_mmap;
	sc->ops.pollc = ps2fb_pollc;

	vcons_init(&sc->vd, sc, &sc->sd, &sc->ops);
	sc->vd.init_screen = ps2fb_init_screen;
	vcons_init_screen(&sc->vd, &sc->console_screen, 1, &defattr);
	sc->console_screen.scr_flags |= VCONS_SCREEN_IS_STATIC;
	ri = &sc->console_screen.scr_ri;
	sc->sd.textops = &ri->ri_ops;
	sc->sd.capabilities = ri->ri_caps;
	sc->sd.nrows = ri->ri_rows;
	sc->sd.ncols = ri->ri_cols;

	/* Replace the early text console: clear, then redraw the kernel log. */
	(*ri->ri_ops.eraserows)(ri, 0, ri->ri_rows, defattr);
	/*
	 * The kernel console stays the EE SIO line; wsdisplay_cnattach only
	 * provides wsdisplay_cnputc for the mirror, so undo its cn_tab switch.
	 */
	saved_cn = cn_tab;
	wsdisplay_cnattach(&sc->sd, ri, 0, 0, defattr);
	vcons_replay_msgbuf(&sc->console_screen);
	ps2_gs_putc_hook = ps2fb_kputc;

	aa.console = 1;
	aa.scrdata = &sc->sl;
	aa.accessops = &sc->ops;
	aa.accesscookie = &sc->vd;
	config_found(self, &aa, wsemuldisplaydevprint,
	    CFARGS(.iattr = "wsemuldisplaydev"));
	cn_tab = saved_cn;
}
