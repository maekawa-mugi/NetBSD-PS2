#!/bin/sh
# Run inside WSL. Cross-build X11 pieces (PIC, n32) into build/ps2/user-n32-pic/dest.
#   sh build-ps2-x.sh dir...      e.g. external/mit/xorg/lib/libXau
# xsrc lives in $XSRC (default ~/xsrc/usr/xsrc, the NetBSD-11.0 xsrc.tgz).
set -u
cd "$(dirname "$0")"
src=$(pwd)
xsrc=${XSRC:-$HOME/xsrc/usr/xsrc}
printf '#!/bin/sh\nexec %s -E "$@"\n' "$src/build/ps2/user-n32-pic/cc" >"$src/build/ps2/user-n32-pic/cpp"
chmod +x "$src/build/ps2/user-n32-pic/cpp"
# playstation2 builds the server without GLX: drop GLXEXT from the pregenerated xsrc headers.
sed -i 's/^#define GLXEXT 1/\/* #undef GLXEXT *\//' "$xsrc"/external/mit/xorg-server/include/dix-config.h \
    "$xsrc"/external/mit/xorg-server/include/xorg-server.h 2>/dev/null
# Host font tools for the cross build come from the build machine (xfonts-utils).
for t in bdftopcf mkfontscale ucs2any; do
    [ -e "$src/build/ps2/tools/bin/nb$t" ] || ln -s /usr/bin/$t "$src/build/ps2/tools/bin/nb$t"
done
for dir in "$@"; do
    name=$(printf '%s' "$dir" | tr / _)
    log="$src/build/ps2/user-n32-pic/logs/x-$name.log"
    mkdir -p "$src/build/ps2/user-n32-pic/logs"
    xargs_="X11SRCDIR=$xsrc MKX11=yes MKXORG=yes MKXORG_SERVER=yes MKSHARE=yes MKX11FONTS=${MKX11FONTS:-no} MKGCC=no MKLLVM=no NOGCCERROR=yes CPP=$src/build/ps2/user-n32-pic/cpp"
    {
        PIC=yes sh ./build-ps2-user.sh make -C "$dir" obj $xargs_ &&
        PIC=yes sh ./build-ps2-user.sh make -C "$dir" includes $xargs_ &&
        PIC=yes sh ./build-ps2-user.sh make -C "$dir" dependall $xargs_ &&
        PIC=yes sh ./build-ps2-user.sh make -C "$dir" MKUPDATE=no install $xargs_
    } >"$log" 2>&1 && echo "OK $dir" || { echo "FAIL $dir ($log)"; tail -5 "$log" | cut -c1-200; }
done
