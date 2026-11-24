#	$NetBSD$

LIVEIMGBASE=	NetBSD-${DISTRIBVER}-playstation2-live	# gives ${IMGBASE}.img

.include <bsd.own.mk>

# Raw image for the DEV9 HDD: disklabel in sector 1 (no MBR/APA),
# root on wd0a, swap on wd0b.  The kernel itself is booted separately
# as an ELF (kern-PS2WSCONS set, also copied to /netbsd here).
LIVEIMAGEMB?=	2048
SWAPMB?=	128
USE_MBR=	no

KERN_SET=	kern-PS2WSCONS
SETS=		base etc misc rescue text
.if ${MKX11} != "no"
SETS+=		xbase xetc xfont xserver
.endif

RC_CONF_EXTRA=	${.CURDIR}/rc.conf.extra
IMAGEHOSTNAME=	ps2
.if ${MKX11} != "no"
IMGFILE_EXTRA=	${.CURDIR}/xorg.conf		etc/X11/xorg.conf
SPEC_EXTRA=	${.CURDIR}/spec.x11.in
.endif

.include "${.CURDIR}/../../common/bootimage/Makefile.liveimage"
