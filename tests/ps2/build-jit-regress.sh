#!/bin/sh
# Build supplemental tests without replacing the normal shell root or ELF.
set -eu
cd "$(dirname "$0")/../.."
src=$(pwd)
kernel=${KERNEL:-PS2GS}
u="$src/build/ps2/user-n32"
t="$src/build/ps2/tools/bin"
o="$src/build/ps2/root-jit128"
mkdir -p "$o"
cp -a "$src/build/ps2/root-shell/tree" "$o/"
for prog in jit-regress mem128; do
"$u/cc" --sysroot="$u/dest" -O2 -Wall -Wextra -Werror \
    -c "tests/ps2/$prog.c" -o "$o/$prog.o"
"$u/cc" --sysroot="$u/dest" -static -nostdlib -o "$o/tree/bin/$prog" \
    "$u/dest/usr/lib/crt0.o" "$u/dest/usr/lib/crti.o" \
    "$u/dest/usr/lib/crtbegin.o" "$o/$prog.o" \
    -Wl,--start-group "$u/dest/usr/lib/libc.a" "$u/dest/usr/lib/libgcc.a" \
    -Wl,--end-group "$u/dest/usr/lib/crtend.o" "$u/dest/usr/lib/crtn.o"
"$t/mipsel--netbsd-strip" "$o/tree/bin/$prog"
done
"$u/cc" -mno-abicalls -fno-pic -G0 -nostdlib -static \
    -Wl,-e,_start,-Ttext,0x00400000 -o "$o/tree/bin/ee-context" \
    tests/ps2/init-ee-context.S
cp "$src/build/ps2/root-shell/root.mtree" "$o/root.mtree"
cat >>"$o/root.mtree" <<'EOF'
./bin/jit-regress type=file mode=0555
./bin/mem128 type=file mode=0555
./bin/ee-context type=file mode=0555
EOF
"$t/nbmakefs" -B le -t ffs -o version=1 -s 12m \
    -F "$o/root.mtree" "$o/root.fs" "$o/tree"
cp "$src/build/ps2/obj/sys/arch/playstation2/compile/$kernel/netbsd" \
    "$src/build/ps2/netbsd-$kernel-jit128.elf"
"$t/mipsel--netbsd-mdsetimage" -v "$src/build/ps2/netbsd-$kernel-jit128.elf" "$o/root.fs"
cp "$src/build/ps2/obj/sys/arch/playstation2/compile/$kernel/netbsd" \
    "$src/build/ps2/netbsd-$kernel-libc.elf"
"$t/mipsel--netbsd-mdsetimage" -v "$src/build/ps2/netbsd-$kernel-libc.elf" \
    "$src/build/ps2/root/root.fs"
