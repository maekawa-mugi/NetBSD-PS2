#!/bin/sh
# Smallest smoke test: ELFROOT kernel with a 2 MiB md root holding only
# /sbin/init (tests/ps2/init-n32.S, or INIT_SOURCE / INIT_BINARY).
#   sh tests/ps2/build-smoke-root.sh   -> $BUILDTOP/tests/netbsd-ELFROOT-smoke.elf
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
kernel=${KERNEL:-ELFROOT}
out="$TOUT/root-smoke"
need_kernel "$kernel"
rm -rf "$out"; mkdir -p "$out/tree/sbin" "$out/tree/dev"
# Compile-time check of the EE context layout against the kernel headers.
$CC -nostdinc -I "$KOBJ/$kernel" -I "$src/sys" -I "$src/sys/arch" \
    -I "$src/lib/libpthread" -c tests/ps2/ee-context-layout.c -o "$out/ee-context-layout.o"
if [ -n "${INIT_BINARY:-}" ]; then
    cp "$INIT_BINARY" "$out/tree/sbin/init"
else
    $CC -mno-abicalls -fno-pic -G0 -nostdlib -static \
        -Wl,-e,_start,-Ttext,0x00400000 \
        -o "$out/tree/sbin/init" "${INIT_SOURCE:-tests/ps2/init-n32.S}"
fi
cat >"$out/root.mtree" <<'X'
. type=dir mode=0755 uid=0 gid=0
./dev type=dir mode=0755 uid=0 gid=0
./dev/console type=char mode=0600 uid=0 gid=0 device=native,2,0
./sbin type=dir mode=0755 uid=0 gid=0
./sbin/init type=file mode=0555 uid=0 gid=0
X
"$MAKEFS" -B le -t ffs -o version=1 -s 2m -F "$out/root.mtree" "$out/root.fs" "$out/tree"
embed_root "$kernel" "$out/root.fs" "$kernel-smoke"
