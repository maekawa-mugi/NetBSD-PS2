#!/bin/sh
# Standalone BSD IOP sources; PS2SDK is used only for its host IRX packager.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
out="$src/build/ps2/native-iop"
devroot=${PS2DEV:-/usr/local/ps2dev}
cc=${IOP_CC:-$devroot/iop/bin/mipsel-none-elf-gcc}
ld=${IOP_LD:-$devroot/iop/bin/mipsel-none-elf-ld}
objcopy=${IOP_OBJCOPY:-$devroot/iop/bin/mipsel-none-elf-objcopy}
irxgen=${IOP_IRXGEN:-$devroot/ps2sdk/bin/ps2-irxgen}
iop="$src/sys/arch/playstation2/iop"
mkdir -p "$out"
for name in bootstrap smoke smoke_imports usb_irq usb_irq_imports dev9_irq dev9_irq_imports; do
    suffix=c
    case "$name" in *_imports) suffix=S ;; esac
    # The ROM relocator requires a fresh HI16/LO16 pair for every reference.
    # GCC explicit relocs can reuse one HI16 for multiple LO16 instructions.
    "$cc" -march=r3000 -mabi=32 -msoft-float -mno-abicalls -fno-pic -G0 \
        -mno-explicit-relocs \
        -Os -ffreestanding -fno-stack-protector -fno-jump-tables \
        -Wall -Wextra -Werror -c "$iop/$name.$suffix" -o "$out/$name.o"
done
"$ld" -T "$iop/bootstrap.ld" --emit-relocs -o "$out/bootstrap.elf" "$out/bootstrap.o"
"$objcopy" -O binary -j .text "$out/bootstrap.elf" "$out/bootstrap.bin"
"$ld" -T "$iop/module.ld" --emit-relocs -o "$out/smoke.elf" "$out/smoke.o" "$out/smoke_imports.o"
"$irxgen" "$out/smoke.elf" "$out/smoke.irx"
"$ld" -T "$iop/module.ld" --emit-relocs -o "$out/usb_irq.elf" "$out/usb_irq.o" "$out/usb_irq_imports.o"
"$irxgen" "$out/usb_irq.elf" "$out/usb_irq.irx"
"$ld" -T "$iop/module.ld" --emit-relocs -o "$out/dev9_irq.elf" "$out/dev9_irq.o" "$out/dev9_irq_imports.o"
"$irxgen" "$out/dev9_irq.elf" "$out/dev9_irq.irx"
# Some versions of the packager return success even on an invalid input.
python3 - "$out" <<'PY'
from pathlib import Path
import struct
import sys
p = Path(sys.argv[1])
elf = (p / 'bootstrap.elf').read_bytes()
assert elf[:7] == b'\x7fELF\x01\x01\x01'
assert struct.unpack_from('<I', elf, 24)[0] == 0, 'bootstrap must enter at offset 0'
shoff = struct.unpack_from('<I', elf, 32)[0]
shentsize, shnum = struct.unpack_from('<HH', elf, 46)
for i in range(shnum):
    sh = struct.unpack_from('<10I', elf, shoff + i * shentsize)
    assert sh[1] not in (4, 9) or sh[5] == 0, 'bootstrap has relocations'
raw = (p / 'bootstrap.bin').read_bytes()
assert raw and len(raw) % 4 == 0
for (w,) in struct.iter_unpack('<I', raw):
    assert w >> 26 not in (2, 3), 'absolute bootstrap J/JAL'
    assert (w >> 21) & 31 != 28 and (w >> 16) & 31 != 28, 'bootstrap uses gp'
    assert w >> 26 != 0 or (w >> 11) & 31 != 28, 'bootstrap writes gp'
irx = (p / 'smoke.irx').read_bytes()
assert irx[:7] == b'\x7fELF\x01\x01\x01'
assert struct.unpack_from('<H', irx, 16)[0] == 0xff80
phoff = struct.unpack_from('<I', irx, 28)[0]
phentsize, phnum = struct.unpack_from('<HH', irx, 42)
types = set()
for i in range(phnum):
    ph = struct.unpack_from('<8I', irx, phoff + i * phentsize)
    types.add(ph[0])
    assert ph[1] <= len(irx) and ph[4] <= len(irx) - ph[1]
    if ph[0] == 1:
        assert ph[2] == 0 and ph[5] >= ph[4]
assert {1, 0x70000080} <= types
assert len(irx) <= 65536
header = '/* Generated from the standalone BSD IOP sources. */\n'
relay = (p / 'usb_irq.irx').read_bytes()
dev9 = (p / 'dev9_irq.irx').read_bytes()
assert relay[:7] == b'\x7fELF\x01\x01\x01'
assert struct.unpack_from('<H', relay, 16)[0] == 0xff80
assert 52 <= len(relay) <= 65536
for module in (irx, relay, dev9):
    phoff = struct.unpack_from('<I', module, 28)[0]
    phentsize, phnum = struct.unpack_from('<HH', module, 42)
    types = set()
    for i in range(phnum):
        ph = struct.unpack_from('<8I', module, phoff + i * phentsize)
        types.add(ph[0])
        assert ph[1] <= len(module) and ph[4] <= len(module) - ph[1]
        if ph[0] == 1:
            assert ph[2] == 0 and ph[5] >= ph[4]
    assert {1, 0x70000080} <= types
    shoff = struct.unpack_from('<I', module, 32)[0]
    shentsize, shnum = struct.unpack_from('<HH', module, 46)
    for i in range(shnum):
        sh = struct.unpack_from('<10I', module, shoff + i * shentsize)
        if sh[1] != 9:  # SHT_REL
            continue
        pending = False
        for at in range(sh[4], sh[4] + sh[5], 8):
            _, info = struct.unpack_from('<II', module, at)
            kind = info & 255
            if pending:
                assert kind == 6, 'IRX HI16 must immediately precede LO16'
            if kind == 6:
                assert pending, 'IRX reuses HI16: unsupported by tested ROM'
            pending = kind == 5
        assert not pending, 'IRX dangling HI16'
for data, name in ((raw, 'ps2_iop_bootstrap_image'), (irx, 'ps2_iop_smoke_image'),
                   (relay, 'ps2_iop_usb_irq_image'), (dev9, 'ps2_iop_dev9_irq_image')):
    data += bytes(-len(data) % 4)
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    header += 'static const uint32_t %s[] = {\n' % name
    for i in range(0, len(words), 8):
        header += '\t' + ', '.join('0x%08xU' % w for w in words[i:i+8]) + ',\n'
    header += '};\n'
(p / 'ps2_iop_images.h').write_text(header)
print('Independent IOP images: relocation-free bootstrap, smoke/USB IRQ IRX with paired relocs')
PY
