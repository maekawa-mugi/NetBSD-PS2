#!/bin/sh
# HDD test root: the shell root plus hddprobe and wd device nodes, embedded
# in a PS2HDD kernel; also hdd-root.ffs, the same tree for partition a.
# Needs tests/ps2/build-shell-root.sh first.
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
kernel=${KERNEL:-PS2HDD}
shell="$TOUT/root-shell"
out="$TOUT/root-hdd"
[ -f "$shell/root.mtree" ] || { echo "run tests/ps2/build-shell-root.sh first" >&2; exit 1; }
need_kernel "$kernel"
rm -rf "$out"; mkdir -p "$out"
cp -a "$shell/tree" "$out/"
ccstatic "$out/tree/bin/hddprobe" tests/ps2/hddprobe.c
mkdir -p "$out/tree/mnt"
cp "$shell/root.mtree" "$out/root.mtree"
cat >>"$out/root.mtree" <<'X'
./bin/hddprobe type=file mode=0555
./mnt type=dir mode=0755
./dev/wd0c type=block mode=0600 device=native,1,2
./dev/rwd0c type=char mode=0600 device=native,10,2
./dev/wd1b type=block mode=0600 device=native,1,17
./dev/rwd1b type=char mode=0600 device=native,10,17
./dev/wd1c type=block mode=0600 device=native,1,18
./dev/rwd1c type=char mode=0600 device=native,10,18
X
"$MAKEFS" -B le -t ffs -o version=1 -s 16m -F "$out/root.mtree" "$out/root.fs" "$out/tree"
embed_root "$kernel" "$out/root.fs" "$kernel-hdd"
# Root-on-HDD image (partition a of the HDD): the same tree, plus wd0a/rwd0a nodes.
cp "$out/root.mtree" "$out/hdd-root.mtree"
cat >>"$out/hdd-root.mtree" <<'X'
./dev/wd0a type=block mode=0600 device=native,1,0
./dev/rwd0a type=char mode=0600 device=native,10,0
X
"$MAKEFS" -B le -t ffs -o version=1 -s 128m -F "$out/hdd-root.mtree" "$out/hdd-root.ffs" "$out/tree"
