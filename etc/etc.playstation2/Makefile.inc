#	$NetBSD: Makefile.inc,v 1.8 2014/03/31 11:25:48 martin Exp $
#
#	etc.playstation2/Makefile.inc --
#		playstation2-specific etc Makefile targets
#

# If you change the list of distributed kernels, don't forget
# to update the release documentation in distrib/notes/common/contents

# PS2WSCONS: ELF-booted (no SBIOS) kernel with GS wsdisplay, USB input,
# DEV9 Ethernet/HDD and root on wd0a; see distrib/playstation2/liveimage.
KERNEL_SETS=		PS2WSCONS
