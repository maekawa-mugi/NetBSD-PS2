/*	$NetBSD: wdc_spd.c,v 1.30 2023/12/20 15:29:05 thorpej Exp $	*/

/*-
 * Copyright (c) 2001, 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: wdc_spd.c,v 1.30 2023/12/20 15:29:05 thorpej Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include "opt_ps2_nosbios.h"

#define __read_1(a)							\
({									\
	u_int32_t ra_ = (a);						\
	u_int8_t r = (*(volatile u_int8_t *)ra_);			\
									\
	if (ra_ == 0xb400004e)	/* (wdc)STAT  LED off */		\
		SPD_LED_OFF();						\
									\
	(r);								\
})
#define __write_1(a, v)							\
{									\
	u_int32_t wa_ = (a);						\
	(*(volatile u_int8_t *)wa_) = (v);				\
									\
	if (wa_ == 0xb400004e)	/* (wdc)CMD  LED on */			\
		SPD_LED_ON();						\
}
#define _PLAYSTATION2_BUS_SPACE_PRIVATE
#include <machine/bus.h>

#include <dev/ata/atavar.h>
#include <dev/ic/wdcvar.h>

#include <playstation2/ee/eevar.h>
#ifdef PS2_NOSBIOS
#include <playstation2/ee/sif_native.h>
#include <playstation2/iop/dev9_irq.h>
#endif
#include <playstation2/dev/spdvar.h>
#include <playstation2/dev/spdreg.h>

#define	WDC_SPD_HDD_AUXREG_OFFSET		0x1c

struct wdc_spd_softc {
	struct wdc_softc sc_wdcdev;
	struct ata_channel *sc_chanlist[1];
	struct ata_channel sc_channel;
	struct wdc_regs sc_wdc_regs;
	void *sc_ih;
#ifdef PS2_NOSBIOS
	/* SPEED DMA runs through IOP DMAC channel 8 into a bounce buffer in IOP RAM. */
	uint32_t sc_dma_iop;		/* IOP address of the bounce buffer */
	volatile uint32_t *sc_dma_win;	/* uncached EE view of it */
	void *sc_dma_buf;		/* kernel buffer of the active transfer */
	size_t sc_dma_len;
	bool sc_dma_read, sc_dma_active, sc_dma_start_error;
	unsigned sc_dma_xfers, sc_dma_errors, sc_dma_fallback;
#endif
};

#ifdef DEBUG
#define STATIC
#else
#define STATIC static
#endif

#ifdef PS2_NOSBIOS
#define	WDC_SPD_DMA_MAX		(256 * 512)
STATIC int wdc_spd_dma_init(void *, int, int, void *, size_t, int);
STATIC void wdc_spd_dma_start(void *, int, int);
STATIC int wdc_spd_dma_finish(void *, int, int, int);
STATIC void wdc_spd_setup_channel(struct ata_channel *);
STATIC bool wdc_spd_dma_setup(struct wdc_spd_softc *);
STATIC bool wdc_spd_shutdown(device_t, int);
#endif
STATIC int wdc_spd_match(device_t, cfdata_t, void *);
STATIC void wdc_spd_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(wdc_spd, sizeof (struct wdc_spd_softc),
    wdc_spd_match, wdc_spd_attach, NULL, NULL);

extern struct cfdriver wdc_cd;

STATIC void __wdc_spd_enable(void);
STATIC void __wdc_spd_disable(void) __attribute__((__unused__));
STATIC void __wdc_spd_bus_space(struct wdc_regs *);

/*
 * wdc register is 16 bit wide.
 */
#define VADDR(h, o)	((h) + (o))
_BUS_SPACE_READ(_wdc_spd, 1, 8)
_BUS_SPACE_READ(_wdc_spd, 2, 16)
_BUS_SPACE_READ_MULTI(_wdc_spd, 1, 8)
_BUS_SPACE_READ_MULTI(_wdc_spd, 2, 16)
_BUS_SPACE_READ_REGION(_wdc_spd, 1, 8)
_BUS_SPACE_READ_REGION(_wdc_spd, 2, 16)
_BUS_SPACE_WRITE(_wdc_spd, 1, 8)
_BUS_SPACE_WRITE(_wdc_spd, 2, 16)
_BUS_SPACE_WRITE_MULTI(_wdc_spd, 1, 8)
_BUS_SPACE_WRITE_MULTI(_wdc_spd, 2, 16)
_BUS_SPACE_WRITE_REGION(_wdc_spd, 1, 8)
_BUS_SPACE_WRITE_REGION(_wdc_spd, 2, 16)
_BUS_SPACE_SET_MULTI(_wdc_spd, 1, 8)
_BUS_SPACE_SET_MULTI(_wdc_spd, 2, 16)
_BUS_SPACE_SET_REGION(_wdc_spd, 1, 8)
_BUS_SPACE_SET_REGION(_wdc_spd, 2, 16)
_BUS_SPACE_COPY_REGION(_wdc_spd, 1, 8)
_BUS_SPACE_COPY_REGION(_wdc_spd, 2, 16)
#undef VADDR

STATIC const struct playstation2_bus_space _wdc_spd_space = {
	pbs_map		: _BUS_SPACE_NO_MAP,
	pbs_unmap	: _BUS_SPACE_NO_UNMAP,
	pbs_subregion	: _BUS_SPACE_NO_SUBREGION,
	pbs_alloc	: _BUS_SPACE_NO_ALLOC,
	pbs_free	: _BUS_SPACE_NO_FREE,
	pbs_vaddr	: _BUS_SPACE_NO_VADDR,
	pbs_r_1		: _wdc_spd_read_1,
	pbs_r_2		: _wdc_spd_read_2,
	pbs_r_4		: _BUS_SPACE_NO_READ(4, 32),
	pbs_r_8		: _BUS_SPACE_NO_READ(8, 64),
	pbs_rm_1	: _wdc_spd_read_multi_1,
	pbs_rm_2	: _wdc_spd_read_multi_2,
	pbs_rm_4	: _BUS_SPACE_NO_READ_MULTI(4, 32),
	pbs_rm_8	: _BUS_SPACE_NO_READ_MULTI(8, 64),
	pbs_rr_1	: _wdc_spd_read_region_1,
	pbs_rr_2	: _wdc_spd_read_region_2,
	pbs_rr_4	: _BUS_SPACE_NO_READ_REGION(4, 32),
	pbs_rr_8	: _BUS_SPACE_NO_READ_REGION(8, 64),
	pbs_w_1		: _wdc_spd_write_1,
	pbs_w_2		: _wdc_spd_write_2,
	pbs_w_4		: _BUS_SPACE_NO_WRITE(4, 32),
	pbs_w_8		: _BUS_SPACE_NO_WRITE(8, 64),
	pbs_wm_1	: _wdc_spd_write_multi_1,
	pbs_wm_2	: _wdc_spd_write_multi_2,
	pbs_wm_4	: _BUS_SPACE_NO_WRITE_MULTI(4, 32),
	pbs_wm_8	: _BUS_SPACE_NO_WRITE_MULTI(8, 64),
	pbs_wr_1	: _wdc_spd_write_region_1,
	pbs_wr_2	: _wdc_spd_write_region_2,
	pbs_wr_4	: _BUS_SPACE_NO_WRITE_REGION(4, 32),
	pbs_wr_8	: _BUS_SPACE_NO_WRITE_REGION(8, 64),
	pbs_sm_1	: _wdc_spd_set_multi_1,
	pbs_sm_2	: _wdc_spd_set_multi_2,
	pbs_sm_4	: _BUS_SPACE_NO_SET_MULTI(4, 32),
	pbs_sm_8	: _BUS_SPACE_NO_SET_MULTI(8, 64),
	pbs_sr_1	: _wdc_spd_set_region_1,
	pbs_sr_2	: _wdc_spd_set_region_2,
	pbs_sr_4	: _BUS_SPACE_NO_SET_REGION(4, 32),
	pbs_sr_8	: _BUS_SPACE_NO_SET_REGION(8, 64),
	pbs_c_1		: _wdc_spd_copy_region_1,
	pbs_c_2		: _wdc_spd_copy_region_2,
	pbs_c_4		: _BUS_SPACE_NO_COPY_REGION(4, 32),
	pbs_c_8		: _BUS_SPACE_NO_COPY_REGION(8, 64),
};

int
wdc_spd_match(device_t parent, cfdata_t cf, void *aux)
{
	struct spd_attach_args *spa = aux;
	struct wdc_regs wdr;
	int i, result;

	if (spa->spa_slot != SPD_HDD)
		return (0);

	__wdc_spd_bus_space(&wdr);

	for (i = 0, result = 0; i < 8 && result == 0; i++) { /* up to 8 sec */
		result = wdcprobe(&wdr);
		if (result == 0)
			delay(1000000);
	}
	
	return (result);
}

void
wdc_spd_attach(device_t parent, device_t self, void *aux)
{
	struct spd_attach_args *spa = aux;
	struct wdc_spd_softc *sc = device_private(self);
	struct wdc_softc *wdc = &sc->sc_wdcdev;
	struct ata_channel *ch = &sc->sc_channel;

	aprint_normal(": %s\n", spa->spa_product_name);

	sc->sc_wdcdev.sc_atac.atac_dev = self;
	sc->sc_wdcdev.regs = &sc->sc_wdc_regs;

#ifdef PS2_NOSBIOS
	wdc->sc_atac.atac_cap = ATAC_CAP_DATA16;
	wdc->sc_atac.atac_pio_cap = 4;
	if (ps2sif_dev9_intr_active() && wdc_spd_dma_setup(sc)) {
		wdc->sc_atac.atac_cap |= ATAC_CAP_DMA | ATAC_CAP_UDMA;
		wdc->sc_atac.atac_dma_cap = 2;
		wdc->sc_atac.atac_udma_cap = 4;
		wdc->sc_atac.atac_set_modes = wdc_spd_setup_channel;
		wdc->dma_arg = sc;
		wdc->dma_init = wdc_spd_dma_init;
		wdc->dma_start = wdc_spd_dma_start;
		wdc->dma_finish = wdc_spd_dma_finish;
		pmf_device_register1(self, NULL, NULL, wdc_spd_shutdown);
	}
#else
	wdc->sc_atac.atac_cap =
	    ATAC_CAP_DMA | ATAC_CAP_UDMA | ATAC_CAP_DATA16;
#endif
#ifndef PS2_NOSBIOS
	wdc->sc_atac.atac_pio_cap = 0;
#endif
	sc->sc_chanlist[0] = &sc->sc_channel;
	wdc->sc_atac.atac_channels = sc->sc_chanlist;
	wdc->sc_atac.atac_nchannels = 1;
	ch->ch_channel = 0;
	ch->ch_atac = &sc->sc_wdcdev.sc_atac;
	wdc->wdc_maxdrives = 2;

	__wdc_spd_bus_space(CHAN_TO_WDC_REGS(ch));

	spd_intr_establish(SPD_HDD, wdcintr, &sc->sc_channel);

	__wdc_spd_enable();

	wdcattach(&sc->sc_channel);
}

void
__wdc_spd_bus_space(struct wdc_regs *wdr)
{
	int i;

	wdr->cmd_iot = &_wdc_spd_space;
	for (i = 0; i < 8; i++)
		wdr->cmd_iohs[i] = SPD_HDD_IO_BASE + i * 2; /*  wdc register is 16 bit wide. */
	wdc_init_shadow_regs(wdr);
	wdr->ctl_iot = &_wdc_spd_space;
	wdr->ctl_ioh = SPD_HDD_IO_BASE + WDC_SPD_HDD_AUXREG_OFFSET;
	wdr->data32iot = wdr->cmd_iot;
	wdr->data32ioh = SPD_HDD_IO_BASE;
}

void
__wdc_spd_enable(void)
{
	u_int16_t r;

	r = spd_intr_enable_get();
	r |= SPD_INTR_HDD;
	spd_intr_enable_set(r);
}

void
__wdc_spd_disable(void)
{
	u_int16_t r;

	r = spd_intr_enable_get();
	r &= ~SPD_INTR_HDD;
	spd_intr_enable_set(r);
}

#ifdef PS2_NOSBIOS
/*
 * SPEED ATA DMA. Data moves between the SPEED FIFO and a bounce buffer in
 * IOP RAM through IOP DMAC channel 8, which only the resident IOP module can
 * program (SIF user command). The EE copies to/from the buffer through the
 * uncached IOP window. Mode register values are the ones the PCSX2 DEV9
 * model documents for the SPEED chip.
 */
static const u_int16_t wdc_spd_pio_regs[] = { 0x92, 0x72, 0x32, 0x24, 0x23 };
static const u_int16_t wdc_spd_mdma_regs[] = { 0xff, 0x45, 0x24 };
static const u_int16_t wdc_spd_udma_regs[] = { 0xa7, 0x85, 0x63, 0x62, 0x61 };

STATIC bool
wdc_spd_dma_setup(struct wdc_spd_softc *sc)
{
	if (ps2sif_alloc(WDC_SPD_DMA_MAX, &sc->sc_dma_iop) != 0)
		return false;
	if ((sc->sc_dma_iop & 15) != 0) {
		(void)ps2sif_free(sc->sc_dma_iop);
		return false;
	}
	sc->sc_dma_win = ps2sif_iop_window(sc->sc_dma_iop);
	return sc->sc_dma_win != NULL;
}

STATIC bool
wdc_spd_shutdown(device_t self, int flags)
{
	struct wdc_spd_softc *sc = device_private(self);

	(void)flags;
	aprint_normal_dev(self, "SPEED DMA: transfers=%u errors=%u PIO fallbacks=%u\n",
	    sc->sc_dma_xfers, sc->sc_dma_errors, sc->sc_dma_fallback);
	return true;
}

STATIC void
wdc_spd_setup_channel(struct ata_channel *chp)
{
	int drive, pio = 4;

	for (drive = 0; drive < chp->ch_ndrives; drive++) {
		struct ata_drive_datas *drvp = &chp->ch_drive[drive];
		if (drvp->drive_type == ATA_DRIVET_NONE)
			continue;
		if (drvp->PIO_mode < pio)
			pio = drvp->PIO_mode;
	}
	if (pio < 0)
		pio = 0;
	_reg_write_2(SPD_PIO_MODE_REG16, wdc_spd_pio_regs[pio]);
}

STATIC void
wdc_spd_dma_off(struct wdc_spd_softc *sc)
{
	u_int16_t r = _reg_read_2(SPD_IF_CTRL_REG16);

	_reg_write_2(SPD_XFR_CTRL_REG16, 0);
	r = (r | SPD_IF_CTRL_HDD_RUN) &
	    ~(SPD_IF_CTRL_DMA_EN | SPD_IF_CTRL_ATA_RST);
	_reg_write_2(SPD_IF_CTRL_REG16, r);
	sc->sc_dma_active = false;
}

STATIC int
wdc_spd_dma_init(void *v, int channel, int drive, void *databuf,
    size_t datalen, int flags)
{
	struct wdc_spd_softc *sc = v;
	struct ata_drive_datas *drvp = &sc->sc_channel.ch_drive[drive];
	bool read = (flags & WDC_DMA_READ) != 0;
	bool udma = (drvp->drive_flags & ATA_DRIVE_UDMA) != 0;
	u_int16_t r;

	KASSERT(channel == 0);
	if (sc->sc_dma_active) {
		aprint_error_dev(sc->sc_wdcdev.sc_atac.atac_dev,
		    "DMA init while a transfer is active\n");
		return EIO;
	}
	if ((flags & (WDC_DMA_PIOBM_ATA | WDC_DMA_PIOBM_ATAPI)) != 0 ||
	    datalen == 0 || datalen > WDC_SPD_DMA_MAX || (datalen & 511) != 0 ||
	    (!udma && (drvp->drive_flags & ATA_DRIVE_DMA) == 0)) {
		sc->sc_dma_fallback++;
		return EINVAL;	/* use PIO */
	}
	if (!read)
		memcpy(__UNVOLATILE(sc->sc_dma_win), databuf, datalen);

	if (udma) {
		_reg_write_2(SPD_UDMA_MODE_REG16,
		    wdc_spd_udma_regs[drvp->UDMA_mode]);
	} else {
		_reg_write_2(SPD_MDMA_MODE_REG16,
		    wdc_spd_mdma_regs[drvp->DMA_mode]);
	}
	_reg_write_2(SPD_DMA_CTRL_REG16, SPD_DMA_FASTEST | SPD_DMA_WIDE);
	_reg_write_2(SPD_DBUF_STAT_REG16, SPD_DBUF_RESET_BOTH);
	r = _reg_read_2(SPD_IF_CTRL_REG16);
	r |= SPD_IF_CTRL_HDD_RUN | SPD_IF_CTRL_DMA_EN;	/* never pulse HDD reset */
	r &= ~(SPD_IF_CTRL_ATA_RST | SPD_IF_CTRL_READ | SPD_IF_CTRL_UDMA);
	if (read)
		r |= SPD_IF_CTRL_READ;
	if (udma)
		r |= SPD_IF_CTRL_UDMA;
	_reg_write_2(SPD_IF_CTRL_REG16, r);
	_reg_write_2(SPD_XFR_CTRL_REG16, SPD_XFR_DMAEN | SPD_XFR_BITS12 |
	    (read ? 0 : SPD_XFR_WRITE));
	sc->sc_dma_buf = databuf;
	sc->sc_dma_len = datalen;
	sc->sc_dma_read = read;
	sc->sc_dma_active = true;
	sc->sc_dma_start_error = false;

	return 0;
}

STATIC void
wdc_spd_dma_start(void *v, int channel, int drive)
{
	struct wdc_spd_softc *sc = v;
	int error;

	/*
	 * Arm the IOP DMAC after the ATA command: SPEED streams transfers
	 * larger than its 8 KiB FIFO only while a started DMA is pending.
	 */
	(void)channel; (void)drive;
	error = ps2sif_dev9_dma(sc->sc_dma_read ? PS2_DEV9_DMA_READ :
	    PS2_DEV9_DMA_WRITE, sc->sc_dma_iop, (uint32_t)sc->sc_dma_len);
	if (error) {
		aprint_error_dev(sc->sc_wdcdev.sc_atac.atac_dev,
		    "IOP DMA start failed: %d\n", error);
		sc->sc_dma_start_error = true;
	}
}

STATIC int
wdc_spd_dma_finish(void *v, int channel, int drive, int force)
{
	struct wdc_spd_softc *sc = v;
	int status = 0, n;

	(void)channel; (void)drive;
	if (!sc->sc_dma_active)
		return WDC_DMAST_NOIRQ;
	if (force != WDC_DMAEND_END || sc->sc_dma_start_error) {
		(void)ps2sif_dev9_dma(PS2_DEV9_DMA_ABORT, 0, 0);
		wdc_spd_dma_off(sc);
		sc->sc_dma_errors++;
		return WDC_DMAST_ERR;
	}
	/* The ATA interrupt can precede the end of the IOP DMA on slow paths. */
	for (n = 0; n < 20000; n++) {
		if (ps2sif_dev9_dma_state() == PS2_DEV9_DMA_DONE)
			break;
		if (sc->sc_dma_read &&
		    (_reg_read_2(SPD_DBUF_STAT_REG16) & SPD_DBUF_AVAIL_MASK) == 0)
			break;
		delay(5);
	}
	if (ps2sif_dev9_dma_state() != PS2_DEV9_DMA_DONE &&
	    !(sc->sc_dma_read &&
	    (_reg_read_2(SPD_DBUF_STAT_REG16) & SPD_DBUF_AVAIL_MASK) == 0)) {
		unsigned cmds, state, bytes;
		status = WDC_DMAST_ERR;
		ps2sif_dev9_dma_info(&cmds, &state, &bytes);
		aprint_error_dev(sc->sc_wdcdev.sc_atac.atac_dev,
		    "DMA incomplete: iop cmds=%u state=%u bytes=%u dbuf=%#x intr=%#x\n",
		    cmds, state, bytes, _reg_read_2(SPD_DBUF_STAT_REG16),
		    _reg_read_2(SPD_INTR_STATUS_REG16));
		(void)ps2sif_dev9_dma(PS2_DEV9_DMA_ABORT, 0, 0);
		sc->sc_dma_errors++;
	} else if (sc->sc_dma_read)
		memcpy(sc->sc_dma_buf, __UNVOLATILE(sc->sc_dma_win),
		    sc->sc_dma_len);
	sc->sc_dma_xfers++;
	wdc_spd_dma_off(sc);
	return status;
}
#endif
