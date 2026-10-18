#!/bin/sh
# Supplemental network test image; does not add test policy to the shell root.
set -eu
cd "$(dirname "$0")/../.."
kernel=${KERNEL:-PS2NET}
KERNEL="$kernel" sh tests/ps2/build-jit-regress.sh
src=$(pwd)
u="$src/build/ps2/user-n32"
t="$src/build/ps2/tools/bin"
o="$src/build/ps2/root-net"
mkdir -p "$o"
cp -a "$src/build/ps2/root-jit128/tree" "$o/"
"$u/cc" --sysroot="$u/dest" -O2 -Wall -Wextra -Werror -c tests/ps2/netprobe.c -o "$o/netprobe.o"
"$u/cc" --sysroot="$u/dest" -static -nostdlib -o "$o/tree/bin/netprobe" \
    "$u/dest/usr/lib/crt0.o" "$u/dest/usr/lib/crti.o" "$u/dest/usr/lib/crtbegin.o" \
    "$o/netprobe.o" -Wl,--start-group "$u/dest/usr/lib/libc.a" "$u/dest/usr/lib/libgcc.a" \
    -Wl,--end-group "$u/dest/usr/lib/crtend.o" "$u/dest/usr/lib/crtn.o"
"$t/mipsel--netbsd-strip" "$o/tree/bin/netprobe"
cp "$src/build/ps2/root-jit128/root.mtree" "$o/root.mtree"
printf './bin/netprobe type=file mode=0555\n' >>"$o/root.mtree"
"$t/nbmakefs" -B le -t ffs -o version=1 -s 16m -F "$o/root.mtree" "$o/root.fs" "$o/tree"
cp "$src/build/ps2/obj/sys/arch/playstation2/compile/$kernel/netbsd" "$src/build/ps2/netbsd-$kernel-net.elf"
"$t/mipsel--netbsd-mdsetimage" -v "$src/build/ps2/netbsd-$kernel-net.elf" "$o/root.fs"
