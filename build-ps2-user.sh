#!/bin/sh
# Experimental static n32/R5900 userspace, isolated from kernel objects.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
out="$src/build/ps2/user-n32"
jobs=${JOBS:-16}
mkdir -p "$out/dest" "$out/obj" "$out/logs"
cat >"$out/cc" <<EOF
#!/bin/sh
exec "$src/build/ps2/tools/bin/mipsel--netbsd-gcc" -march=r5900 -mabi=n32 -msoft-float -mno-llsc -Wl,-m,elf32ltsmipn32 "\$@"
EOF
chmod +x "$out/cc"

umake()
{
    env MACHINE=playstation2 MACHINE_ARCH=mips64el \
        MACHINE_GNU_PLATFORM=mipsel--netbsd MAKECONF=/dev/null \
        TOOLDIR="$src/build/ps2/tools" USETOOLS=yes \
        NETBSDSRCDIR="$src" MKUNPRIVED=yes MKUPDATE=yes MKARZERO=yes \
        HOST_CC=/usr/bin/gcc HOST_CXX=/usr/bin/g++ HOST_CFLAGS=-O2 \
        "MAKEOBJDIR=\${.CURDIR:C,^$src,$out/obj,}" \
        "DESTDIR=$out/dest" "RELEASEDIR=$out/release" \
        "$src/build/ps2/tools/bin/nbmake" -de -m "$src/share/mk" -j "$jobs" \
        "CC=$out/cc" \
        "LD=$src/build/ps2/tools/bin/mipsel--netbsd-ld -m elf32ltsmipn32" \
        'CPUFLAGS=-march=r5900 -mabi=n32 -msoft-float -mno-llsc -Wl,-m,elf32ltsmipn32' \
        MKSOFTFLOAT=yes MKCOMPAT=no MKPIC=no MKPIE=no MKPROFILE=no \
        MKDEBUG=no MKDEBUGLIB=no MKLINT=no MKMAN=no MKHTML=no MKDOC=no \
        MKDTRACE=no MKCXX=no MKSSP=no MKFORTIFY=no MKRELRO=no "$@"
}

case ${1:-headers} in
all)
    for phase in toolchain headers libs init; do
        sh "$src/build-ps2-user.sh" "$phase"
    done
    ;;
toolchain)
    if [ ! -f "$src/build/ps2/obj/tools/gcc/build/Makefile" ]; then
        sh ./build-ps2.sh
    fi
    make -C "$src/build/ps2/obj/tools/gcc/build" -j "$jobs" all-gcc \
        >"$out/logs/toolchain.log" 2>&1
    make -C "$src/build/ps2/obj/tools/gcc/build" install-gcc \
        >>"$out/logs/toolchain.log" 2>&1
    ;;
headers)
    umake obj >"$out/logs/obj.log" 2>&1
    umake do-distrib-dirs >"$out/logs/dirs.log" 2>&1
    umake includes >"$out/logs/includes.log" 2>&1
    ;;
libs)
    for subdir in lib/csu external/gpl3/gcc/lib/libgcc/libgcc lib/libc; do
        name=$(printf '%s' "$subdir" | tr / _)
        umake -C "$src/$subdir" obj >"$out/logs/$name.log" 2>&1
        # Rebuild after compiler/ABI option changes; never reuse mixed objects.
        umake -C "$src/$subdir" cleandir >>"$out/logs/$name.log" 2>&1
        umake -C "$src/$subdir" dependall >>"$out/logs/$name.log" 2>&1
        umake -C "$src/$subdir" MKUPDATE=no install >>"$out/logs/$name.log" 2>&1
    done
    ;;
make)
    shift
    umake "$@"
    ;;
init)
    "$out/cc" --sysroot="$out/dest" -O2 -Wall -Wextra -Werror \
        -c tests/ps2/init-libc.c -o "$out/init-libc.o"
    "$out/cc" --sysroot="$out/dest" -static -nostdlib \
        -o "$out/init-libc" "$out/dest/usr/lib/crt0.o" \
        "$out/dest/usr/lib/crti.o" "$out/dest/usr/lib/crtbegin.o" \
        "$out/init-libc.o" \
        -Wl,--start-group "$out/dest/usr/lib/libc.a" \
        "$out/dest/usr/lib/libgcc.a" -Wl,--end-group \
        "$out/dest/usr/lib/crtend.o" "$out/dest/usr/lib/crtn.o"
    INIT_BINARY="$out/init-libc" sh ./build-ps2-root.sh
    ;;
*) echo 'usage: sh build-ps2-user.sh {all|toolchain|headers|libs|init|make ...}' >&2; exit 1 ;;
esac
