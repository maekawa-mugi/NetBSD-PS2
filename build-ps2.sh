#!/bin/sh
# Run inside WSL/Linux. Keep host settings separate from /etc/mk.conf.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
jobs=${JOBS:-8}
kernel=${KERNEL:-ELFBOOT}
mkdir -p build/ps2/logs

build()
{
    ./build.sh -U -u -m playstation2 -j "$jobs" \
        -V MAKECONF=/dev/null \
        -V HOST_CC=/usr/bin/gcc -V HOST_CXX=/usr/bin/g++ -V HOST_CFLAGS=-O2 \
        -O "$src/build/ps2/obj" -T "$src/build/ps2/tools" "$@"
}

build tools >build/ps2/logs/tools.log 2>&1
case "$kernel" in
PS2IOPBOOT|PS2NET|PS2HDD|PS2HDDROOT|PS2WSCONS) sh ./build-ps2-iop.sh >build/ps2/logs/native-iop.log 2>&1 ;;
esac
build "kernel=$kernel" >build/ps2/logs/kernel.log 2>&1
cp "build/ps2/obj/sys/arch/playstation2/compile/$kernel/netbsd" \
    "build/ps2/netbsd-$kernel.elf"
printf 'Kernel ELF: %s/build/ps2/netbsd-%s.elf\n' "$src" "$kernel"
