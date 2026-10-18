#!/bin/sh
# HDD test root: adds hddprobe and wd device nodes. Does NOT rebuild the kernel;
# run KERNEL=PS2HDD sh build-ps2-shell.sh first.
set -eu
cd "$(dirname "$0")/../.."
kernel=${KERNEL:-PS2HDD}
src=$(pwd)
u="$src/build/ps2/user-n32"
t="$src/build/ps2/tools/bin"
o="$src/build/ps2/root-hdd"
mkdir -p "$o"
rm -rf "$o/tree"
cp -a "$src/build/ps2/root-shell/tree" "$o/"
"$u/cc" --sysroot="$u/dest" -O2 -Wall -Wextra -Werror -c tests/ps2/hddprobe.c -o "$o/hddprobe.o"
"$u/cc" --sysroot="$u/dest" -static -nostdlib -o "$o/tree/bin/hddprobe" \
    "$u/dest/usr/lib/crt0.o" "$u/dest/usr/lib/crti.o" "$u/dest/usr/lib/crtbegin.o" \
    "$o/hddprobe.o" -Wl,--start-group "$u/dest/usr/lib/libc.a" "$u/dest/usr/lib/libgcc.a" \
    -Wl,--end-group "$u/dest/usr/lib/crtend.o" "$u/dest/usr/lib/crtn.o"
"$t/mipsel--netbsd-strip" "$o/tree/bin/hddprobe"
mkdir -p "$o/tree/mnt"
cp "$src/build/ps2/root-shell/root.mtree" "$o/root.mtree"
cat >>"$o/root.mtree" <<'EOT'
./bin/hddprobe type=file mode=0555
./mnt type=dir mode=0755
./dev/wd0c type=block mode=0600 device=native,1,2
./dev/rwd0c type=char mode=0600 device=native,10,2
./dev/wd1b type=block mode=0600 device=native,1,17
./dev/rwd1b type=char mode=0600 device=native,10,17
./dev/wd1c type=block mode=0600 device=native,1,18
./dev/rwd1c type=char mode=0600 device=native,10,18
EOT
"$t/nbmakefs" -B le -t ffs -o version=1 -s 16m -F "$o/root.mtree" "$o/root.fs" "$o/tree"
cp "$src/build/ps2/obj/sys/arch/playstation2/compile/$kernel/netbsd" "$src/build/ps2/netbsd-$kernel-hdd.elf"
"$t/mipsel--netbsd-mdsetimage" -v "$src/build/ps2/netbsd-$kernel-hdd.elf" "$o/root.fs"
# Root-on-HDD image (partition a of the HDD): the same tree, plus wd0a/rwd0a nodes.
cp "$o/root.mtree" "$o/hdd-root.mtree"
cat >>"$o/hdd-root.mtree" <<'EOT'
./dev/wd0a type=block mode=0600 device=native,1,0
./dev/rwd0a type=char mode=0600 device=native,10,0
./root type=dir mode=0755
EOT
"$t/nbmakefs" -B le -t ffs -o version=1 -s 128m -F "$o/hdd-root.mtree" "$o/hdd-root.ffs" "$o/tree"
