#	$NetBSD: Makefile,v 1.17 2025/09/23 20:12:08 rillig Exp $

# The kernel is loaded as a PS2 ELF (no SBIOS) and mounts its root from the
# DEV9 HDD (wd0a), so there is no installer: the live image built by
# "build.sh -m playstation2 release live-image" is the distribution.

SUBDIR=		liveimage
TARGETS+=	release

live_image: .MAKE
	${MAKEDIRTARGET} liveimage live_image

.include <bsd.subdir.mk>
