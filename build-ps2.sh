#!/bin/sh
# Convenience wrapper around build.sh for NetBSD/playstation2.
#
#   sh build-ps2.sh                    tools, release (kernel + sets), live-image
#   X=yes XSRC=/path/to/xsrc sh build-ps2.sh     the same with X11 (build.sh -x)
#   sh build-ps2.sh kernel=PS2HDD      any build.sh operations instead
#
# Everything is plain build.sh -m playstation2; this only fixes the output
# directories (build-ps2.conf, overridable from the environment) so the
# other build-ps2-*.sh scripts can find TOOLDIR/DESTDIR/RELEASEDIR.
# Results:
#   $RELEASEDIR/playstation2/binary/kernel/netbsd-PS2WSCONS.gz   PS2 ELF kernel
#   $RELEASEDIR/playstation2/binary/sets/*.tgz                  sets
#   $RELEASEDIR/images/NetBSD-*-playstation2-live.img.gz        HDD image
set -eu
cd "$(dirname "$0")"
src=$(pwd)
. ./build-ps2.conf

xflags=
if [ "${X:-no}" = yes ]; then
    [ -n "$XSRC" ] || { echo "X=yes needs XSRC=/path/to/xsrc (NetBSD xsrc checkout)" >&2; exit 1; }
    xflags="-x -X $XSRC"
fi

[ $# -gt 0 ] || set -- tools release live-image

exec ./build.sh -U -u -m playstation2 -j "$JOBS" $xflags \
    -O "$OBJDIR" -T "$TOOLDIR" -D "$DESTDIR" -R "$RELEASEDIR" "$@"
