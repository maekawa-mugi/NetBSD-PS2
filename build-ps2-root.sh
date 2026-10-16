#!/bin/sh
# Run inside WSL. Build a smoke-test init (or use INIT_BINARY) and embed FFS.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
tools="$src/build/ps2/tools/bin"
out="$src/build/ps2/root"
kernel=${KERNEL:-ELFROOT}
KERNEL="$kernel" sh ./build-ps2.sh
mkdir -p "$out/tree/sbin" "$out/tree/dev"
"$tools/mipsel--netbsd-gcc" -mabi=n32 -march=r5900 -msoft-float \
    -nostdinc -I "$src/build/ps2/obj/sys/arch/playstation2/compile/$kernel" \
    -I "$src/sys" -I "$src/sys/arch" -I "$src/lib/libpthread" \
    -c tests/ps2/ee-context-layout.c -o "$out/ee-context-layout.o"
if [ -n "${INIT_BINARY:-}" ]; then
    cp "$INIT_BINARY" "$out/tree/sbin/init"
else
"$tools/mipsel--netbsd-gcc" -mabi=n32 -march=r5900 -msoft-float \
    -mno-abicalls -fno-pic -G0 -nostdlib -static \
    -Wl,-m,elf32ltsmipn32,-e,_start,-Ttext,0x00400000 \
    -o "$out/tree/sbin/init" "${INIT_SOURCE:-tests/ps2/init-n32.S}"
fi
cat >"$out/root.mtree" <<'EOF'
. type=dir mode=0755 uid=0 gid=0
./dev type=dir mode=0755 uid=0 gid=0
./dev/console type=char mode=0600 uid=0 gid=0 device=native,2,0
./sbin type=dir mode=0755 uid=0 gid=0
./sbin/init type=file mode=0555 uid=0 gid=0
EOF
"$tools/nbmakefs" -B le -t ffs -o version=1 -s 2m \
    -F "$out/root.mtree" "$out/root.fs" "$out/tree"
"$tools/mipsel--netbsd-mdsetimage" -v \
    "$src/build/ps2/netbsd-$kernel.elf" "$out/root.fs"
