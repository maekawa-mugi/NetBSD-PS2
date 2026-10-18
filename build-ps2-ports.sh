#!/bin/sh
# Run inside WSL. Cross-build third-party (pkgsrc-sourced) programs for the PS2 userland.
#   sh build-ps2-ports.sh jpeg png fltk dillo
# pkgsrc itself is not used to build (it has no cross support here); distfiles/versions
# follow ~/pkgsrc (see $PKGSRC) and are checked against its distinfo SHA512.
# Everything is PIC n32/R5900 soft-float, installed under /usr/pkg inside
# build/ps2/user-n32-pic/dest, which is the sysroot later ports link against.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
pic="$src/build/ps2/user-n32-pic"
tools="$src/build/ps2/tools/bin"
pkgsrc=${PKGSRC:-$HOME/pkgsrc}
dist=${PS2PORTS:-$HOME/ps2ports}/dist
work="$src/build/ps2/ports"
sysroot="$pic/dest"
prefix=/usr/pkg
mkdir -p "$work/logs" "$dist"

export CC="$tools/mipsel--netbsd-gcc -march=r5900 -mabi=n32 -msoft-float -mno-llsc -Wl,-m,elf32ltsmipn32 --sysroot=$sysroot"
export CXX="$tools/mipsel--netbsd-g++ -march=r5900 -mabi=n32 -msoft-float -mno-llsc -Wl,-m,elf32ltsmipn32 --sysroot=$sysroot"
export AR="$tools/mipsel--netbsd-ar" RANLIB="$tools/mipsel--netbsd-ranlib" STRIP="$tools/mipsel--netbsd-strip"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot$prefix/lib/pkgconfig:$sysroot/usr/X11R7/lib/pkgconfig:$sysroot/usr/lib/pkgconfig"
export PKG_CONFIG_PATH=
host=mipsel--netbsd
sx="$sysroot/usr/X11R7"
export CPPFLAGS="-I$sysroot$prefix/include -I$sx/include"
export LDFLAGS="-L$sysroot$prefix/lib -L$sx/lib -Wl,-R$prefix/lib -Wl,-R/usr/X11R7/lib -Wl,-rpath-link,$sysroot$prefix/lib -Wl,-rpath-link,$sx/lib -Wl,-rpath-link,$sysroot/usr/lib"

# name tarball -> unpack into $work/<name>
fetch()
{
    f=$1
    sum=$(grep "^SHA512 ($f)\|^SHA512 (dillo/$f)" "$pkgsrc"/*/*/distinfo 2>/dev/null | head -1 | sed 's/.*= //') || true
    if [ -n "$sum" ] && [ "$(sha512sum "$dist/$f" | cut -d' ' -f1)" != "$sum" ]; then
        echo "checksum mismatch: $f" >&2; exit 1
    fi
}

unpack()
{
    name=$1 tarball=$2
    rm -rf "$work/$name"; mkdir -p "$work/$name"
    tar -C "$work/$name" --strip-components=1 -xf "$dist/$tarball"
}

install_dest()
{
    # make install into the sysroot (so later ports find headers/libs)
    gmake -C "$1" install DESTDIR="$sysroot"
}

for port in "$@"; do
    log="$work/logs/$port.log"
    echo "== $port"
    case $port in
    jpeg)
        fetch jpegsrc.v9f.tar.gz
        unpack jpeg jpegsrc.v9f.tar.gz
        { cd "$work/jpeg" && ./configure --host=$host --prefix=$prefix --enable-shared --disable-static &&
          gmake -j8 && cd "$src" && install_dest "$work/jpeg"; } >"$log" 2>&1
        ;;
    png)
        fetch libpng-1.6.59.tar.gz
        unpack png libpng-1.6.59.tar.gz
        { cd "$work/png" && ./configure --host=$host --prefix=$prefix --enable-shared --disable-static &&
          gmake -j8 && cd "$src" && install_dest "$work/png"; } >"$log" 2>&1
        ;;
    fltk)
        fetch fltk-1.4.5-source.tar.bz2
        unpack fltk fltk-1.4.5-source.tar.bz2
        { cd "$work/fltk" && ./configure --host=$host --prefix=$prefix --enable-shared --enable-threads \
              --enable-xft --disable-wayland --disable-cairo --disable-pango --disable-gl \
              --disable-localjpeg --disable-localpng --disable-localzlib --disable-test &&
          gmake -j8 && cd "$src" && install_dest "$work/fltk"; } >"$log" 2>&1
        ;;
    dillo)
        fetch dillo-v3.3.0.tar.gz
        unpack dillo dillo-v3.3.0.tar.gz
        # fltk-config reports /usr/pkg paths; point it at the sysroot copy.
        mkdir -p "$work/bin"
        cat >"$work/bin/fltk-config" <<EOF
#!/bin/sh
exec sh $sysroot$prefix/bin/fltk-config "\$@" | sed "s,-I$prefix/,-I$sysroot$prefix/,g;s,-L$prefix/,-L$sysroot$prefix/,g;s,$prefix/lib/lib,$sysroot$prefix/lib/lib,g"
EOF
        chmod +x "$work/bin/fltk-config"
        { cd "$work/dillo" && autoreconf -fi &&
          PATH="$work/bin:$PATH" ./configure --host=$host --prefix=$prefix --sysconfdir=/etc/dillo \
              --enable-experimental-fltk --disable-tls --disable-openssl --disable-mbedtls \
              --disable-ipv6 --disable-webp &&
          gmake -j8 && cd "$src" && install_dest "$work/dillo"; } >"$log" 2>&1
        ;;
    *) echo "unknown port $port" >&2; exit 1 ;;
    esac && echo "OK $port" || { echo "FAIL $port ($log)"; tail -15 "$log" | cut -c1-200; }
done
