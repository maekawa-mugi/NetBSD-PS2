#!/bin/sh
# Network regress root: the JIT root plus netprobe, embedded in a PS2NET
# kernel (pair with tests/ps2/net_echo_host.py on the host).
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
kernel=${KERNEL:-PS2NET}
KERNEL="$kernel" sh tests/ps2/build-jit-regress.sh
jit="$TOUT/root-jit128"
out="$TOUT/root-net"
rm -rf "$out"; mkdir -p "$out"
cp -a "$jit/tree" "$out/"
ccstatic "$out/tree/bin/netprobe" tests/ps2/netprobe.c
cp "$jit/root.mtree" "$out/root.mtree"
printf './bin/netprobe type=file mode=0555\n' >>"$out/root.mtree"
"$MAKEFS" -B le -t ffs -o version=1 -s 16m -F "$out/root.mtree" "$out/root.fs" "$out/tree"
embed_root "$kernel" "$out/root.fs" "$kernel-net"
