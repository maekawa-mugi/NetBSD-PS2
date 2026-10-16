/* Direct EE SIO console for the SBIOS-free ELF smoke-test configuration. */
#include "opt_ps2_gsconsole.h"
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/callout.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <dev/cons.h>
#include <mips/cpuregs.h>

#define SIO_ISR (*(volatile uint32_t *)MIPS_PHYS_TO_KSEG1(0x1000f130))
#define SIO_TX (*(volatile uint8_t *)MIPS_PHYS_TO_KSEG1(0x1000f180))
#define SIO_RX (*(volatile uint8_t *)MIPS_PHYS_TO_KSEG1(0x1000f1c0))

extern const struct cdevsw eesio_cdevsw;
static struct tty *sio_tty;
static callout_t sio_rx_ch;
static bool sio_polling;

#ifdef PS2_GSCONSOLE
void ps2_gs_init(void);
void ps2_gs_putc(int);
#endif

static void
sio_tx(int c)
{

	while (SIO_ISR & 0x8000)
		;
	SIO_TX = c;
#ifdef PS2_GSCONSOLE
	ps2_gs_putc(c);
#endif
}

static void
sio_probe(struct consdev *cn)
{
	cn->cn_dev = makedev(cdevsw_lookup_major(&eesio_cdevsw), 0);
	cn->cn_pri = CN_INTERNAL;
}

static void
sio_init(struct consdev *cn)
{
#ifdef PS2_GSCONSOLE
	ps2_gs_init();
#endif
}

static int
sio_getc(dev_t dev)
{
	while ((SIO_ISR & 0xf00) == 0)
		;
	return SIO_RX;
}

static void
sio_putc(dev_t dev, int c)
{
	if (c == '\n')
		sio_putc(dev, '\r');
	sio_tx(c);
}

static void
eesio_start(struct tty *tp)
{
	int s = spltty();
	if (!(tp->t_state & (TS_BUSY | TS_TTSTOP | TS_TIMEOUT))) {
		tp->t_state |= TS_BUSY;
		while (tp->t_outq.c_cc)
			sio_tx(getc(&tp->t_outq));
		tp->t_state &= ~TS_BUSY;
		ttypull(tp);
	}
	splx(s);
}

static int
eesio_param(struct tty *tp, struct termios *t)
{
	/* Firmware/emulator supplies the SIO baud configuration. */
	return 0;
}

static void
eesio_rx(void *arg)
{
	struct tty *tp = arg;
	int s = spltty();
	/* Bound each pass so continuous input cannot starve other work. */
	for (unsigned n = 0; n < 256 && (SIO_ISR & 0xf00); n++) {
		int c = SIO_RX;
		if (tp->t_state & TS_ISOPEN)
			(*tp->t_linesw->l_rint)(c, tp);
	}
	if (!sio_polling)
		callout_schedule(&sio_rx_ch, 1);
	splx(s);
}

static void
eesio_pollc(dev_t dev, int on)
{
	sio_polling = on != 0;
	if (sio_tty == NULL)
		return;
	if (on)
		callout_stop(&sio_rx_ch);
	else if (sio_tty->t_state & TS_ISOPEN)
		callout_schedule(&sio_rx_ch, 1);
}

static int
eesio_open(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct tty *tp;
	int error;
	if (minor(dev) != 0)
		return ENXIO;
	if (sio_tty == NULL) {
		sio_tty = tty_alloc();
		tty_attach(sio_tty);
		callout_init(&sio_rx_ch, 0);
		callout_setfunc(&sio_rx_ch, eesio_rx, sio_tty);
	}
	tp = sio_tty;
	tp->t_dev = dev;
	tp->t_oproc = eesio_start;
	tp->t_param = eesio_param;
	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp))
		return EBUSY;
	if (!(tp->t_state & TS_ISOPEN)) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG | CLOCAL;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = 115200;
		ttsetwater(tp);
	}
	tp->t_state |= TS_CARR_ON;
	error = (*tp->t_linesw->l_open)(dev, tp);
	if (error == 0 && !sio_polling)
		callout_schedule(&sio_rx_ch, 1);
	return error;
}

static int
eesio_close(dev_t dev, int flag, int mode, struct lwp *l)
{
	callout_stop(&sio_rx_ch);
	(*sio_tty->t_linesw->l_close)(sio_tty, flag);
	ttyclose(sio_tty);
	return 0;
}

static int
eesio_read(dev_t dev, struct uio *uio, int flag)
{
	return (*sio_tty->t_linesw->l_read)(sio_tty, uio, flag);
}

static int
eesio_write(dev_t dev, struct uio *uio, int flag)
{
	return (*sio_tty->t_linesw->l_write)(sio_tty, uio, flag);
}

static int
eesio_ioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	int error = (*sio_tty->t_linesw->l_ioctl)(sio_tty, cmd, data, flag, l);
	if (error == EPASSTHROUGH)
		error = ttioctl(sio_tty, cmd, data, flag, l);
	return error;
}

static struct tty *
eesio_tty(dev_t dev)
{
	return minor(dev) == 0 ? sio_tty : NULL;
}

static int
eesio_poll(dev_t dev, int events, struct lwp *l)
{
	return (*sio_tty->t_linesw->l_poll)(sio_tty, events, l);
}

const struct cdevsw eesio_cdevsw = {
	.d_open = eesio_open, .d_close = eesio_close,
	.d_read = eesio_read, .d_write = eesio_write, .d_ioctl = eesio_ioctl,
	.d_stop = nostop, .d_tty = eesio_tty, .d_poll = eesio_poll,
	.d_mmap = nommap, .d_kqfilter = ttykqfilter, .d_discard = nodiscard,
	.d_flag = D_TTY
};

struct consdev constab[] = {
	{ .cn_probe = sio_probe, .cn_init = sio_init,
	  .cn_getc = sio_getc, .cn_putc = sio_putc, .cn_pollc = eesio_pollc },
	{ 0 }
};

void
consinit(void)
{
	static bool initialized;
	if (!initialized) {
		cninit();
		initialized = true;
	}
}
