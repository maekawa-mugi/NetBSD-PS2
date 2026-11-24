#!/bin/sh
# JIT/128-bit memory regress root: the shell root plus jit-regress, mem128
# and ee-context, embedded in a PS2GS kernel.
# Needs tests/ps2/build-shell-root.sh first.
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
kernel=${KERNEL:-PS2GS}
shell="$TOUT/root-shell"
out="$TOUT/root-jit128"
[ -f "$shell/root.mtree" ] || { echo "run tests/ps2/build-shell-root.sh first" >&2; exit 1; }
need_kernel "$kernel"
rm -rf "$out"; mkdir -p "$out"
cp -a "$shell/tree" "$out/"
for prog in jit-regress mem128; do
    ccstatic "$out/tree/bin/$prog" "tests/ps2/$prog.c"
done
$CC -mno-abicalls -fno-pic -G0 -nostdlib -static \
    -Wl,-e,_start,-Ttext,0x00400000 -o "$out/tree/bin/ee-context" \
    tests/ps2/init-ee-context.S
cp "$shell/root.mtree" "$out/root.mtree"
cat >>"$out/root.mtree" <<'X'
./bin/jit-regress type=file mode=0555
./bin/mem128 type=file mode=0555
./bin/ee-context type=file mode=0555
X
"$MAKEFS" -B le -t ffs -o version=1 -s 12m -F "$out/root.mtree" "$out/root.fs" "$out/tree"
embed_root "$kernel" "$out/root.fs" "$kernel-jit128"
