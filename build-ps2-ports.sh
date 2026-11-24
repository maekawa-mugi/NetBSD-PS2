#!/bin/sh
# Cross-build third-party programs (not part of NetBSD base) for playstation2
# against the build.sh DESTDIR, after `sh build-ps2.sh` (with X for fltk/dillo).
#   sh build-ps2-ports.sh jpeg png fltk dillo
# pkgsrc has no cross support here, so each port is configured directly.
# Distfiles are fetched into $PS2PORTS_DISTDIR and checked against the
# SHA512 sums below (the same files/sums as pkgsrc's distinfo).
# Everything installs under /usr/pkg inside DESTDIR, which is also the sysroot
# later ports link against; build-ps2-image.sh copies it into the root image.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
. ./build-ps2.conf
tools="$TOOLDIR/bin"
sysroot="$DESTDIR"
dist=${PS2PORTS_DISTDIR:-$BUILDTOP/distfiles}
work="$BUILDTOP/ports"
prefix=/usr/pkg
host=mips64el--netbsd
mkdir -p "$work/logs" "$dist"

[ -x "$tools/$host-gcc" ] || { echo "no toolchain in $tools: run build-ps2.sh first" >&2; exit 1; }
[ -f "$sysroot/usr/lib/libc.so" ] || { echo "no userland in $sysroot: run build-ps2.sh first" >&2; exit 1; }

export CC="$tools/$host-gcc --sysroot=$sysroot"
export CXX="$tools/$host-g++ --sysroot=$sysroot"
export AR="$tools/$host-ar" RANLIB="$tools/$host-ranlib" STRIP="$tools/$host-strip"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot$prefix/lib/pkgconfig:$sysroot/usr/X11R7/lib/pkgconfig:$sysroot/usr/lib/pkgconfig"
export PKG_CONFIG_PATH=
sx="$sysroot/usr/X11R7"
export CPPFLAGS="-I$sysroot$prefix/include -I$sx/include"
export LDFLAGS="-L$sysroot$prefix/lib -L$sx/lib -Wl,-R$prefix/lib -Wl,-R/usr/X11R7/lib -Wl,-rpath-link,$sysroot$prefix/lib -Wl,-rpath-link,$sx/lib -Wl,-rpath-link,$sysroot/usr/lib"

# fetch file url sha512
fetch()
{
    f=$1 url=$2 sum=$3
    if [ ! -f "$dist/$f" ]; then
        curl -fL -o "$dist/$f.part" "$url" && mv "$dist/$f.part" "$dist/$f"
    fi
    if [ "$(sha512sum "$dist/$f" | cut -d' ' -f1)" != "$sum" ]; then
        echo "checksum mismatch: $dist/$f" >&2; exit 1
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
        fetch jpegsrc.v9f.tar.gz http://www.ijg.org/files/jpegsrc.v9f.tar.gz \
            7f733d79cf176c690dcf127352f9aa7ec48000455944f286faae606cdeada6f6865b4a3f9f01bda8947b5b1089bb3e52d2b56879b6e871279ec5cbd1829304dc
        unpack jpeg jpegsrc.v9f.tar.gz
        { cd "$work/jpeg" && ./configure --host=$host --prefix=$prefix --enable-shared --disable-static &&
          gmake -j"$JOBS" && cd "$src" && install_dest "$work/jpeg"; } >"$log" 2>&1
        ;;
    png)
        fetch libpng-1.6.59.tar.gz https://github.com/pnggroup/libpng/archive/v1.6.59.tar.gz \
            2d753e764846a0a75a10f2a00c6f4ae23d2ae37237c69d59d5e619da3f143d59f4ea9ce29c1fb39435df1cef77877c83b042a2275934c353a6603061da2d2dd9
        unpack png libpng-1.6.59.tar.gz
        { cd "$work/png" && ./configure --host=$host --prefix=$prefix --enable-shared --disable-static &&
          gmake -j"$JOBS" && cd "$src" && install_dest "$work/png"; } >"$log" 2>&1
        ;;
    fltk)
        fetch fltk-1.4.5-source.tar.bz2 https://github.com/fltk/fltk/releases/download/release-1.4.5/fltk-1.4.5-source.tar.bz2 \
            ed0c692c1abe6c1675eb75c8562d9d0449c6db39b53d26423bc6fc6e1b97602e727eadf7d58f8a1f9bfb7d088d7f5aeba01619de626f6728a72469b0feea47aa
        unpack fltk fltk-1.4.5-source.tar.bz2
        { cd "$work/fltk" && ./configure --host=$host --prefix=$prefix --enable-shared --enable-threads \
              --enable-xft --disable-wayland --disable-cairo --disable-pango --disable-gl \
              --disable-localjpeg --disable-localpng --disable-localzlib --disable-test &&
          gmake -j"$JOBS" && cd "$src" && install_dest "$work/fltk"; } >"$log" 2>&1
        ;;
    dillo)
        fetch dillo-v3.3.0.tar.gz https://git.sr.ht/~dillo/dillo/archive/v3.3.0.tar.gz \
            189d73122ba0f557ca295c147d82b525ba779d1a468c730d167a1be6b2ae1beda7de717e8941378f7fe21a4a935f593c31993df4f0e5dc55a6b235cb7c9c8e1f
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
          gmake -j"$JOBS" && cd "$src" && install_dest "$work/dillo"; } >"$log" 2>&1
        ;;
    *) echo "unknown port $port" >&2; exit 1 ;;
    esac && echo "OK $port" || { echo "FAIL $port ($log)"; tail -15 "$log" | cut -c1-200; exit 1; }
done
