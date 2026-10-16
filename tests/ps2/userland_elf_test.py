#!/usr/bin/env python3
"""Check R5900 TLS codegen, generic MIPS behavior, and the static init ISA."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
tools = root / "build/ps2/tools/bin"
cc = tools / "mipsel--netbsd-gcc"
objdump = tools / "mipsel--netbsd-objdump"
elfs = [Path(arg) for arg in sys.argv[1:]] or [root / "build/ps2/user-n32/init-libc"]
source = "__thread int value; int *get_tls(void) { return &value; }\n"
with tempfile.TemporaryDirectory(prefix="ps2-tls-") as td:
    for arch in ("r5900", "mips3"):
        output = Path(td) / (arch + ".s")
        subprocess.run([str(cc), "-mabi=n32", "-march=" + arch,
                        "-msoft-float", "-mno-llsc", "-ftls-model=local-exec", "-O2",
                        "-S", "-x", "c", "-", "-o", str(output)],
                       input=source, text=True, check=True)
        asm = output.read_text()
        if arch == "r5900":
            assert "_lwp_getprivate" in asm, asm
            assert "rdhwr" not in asm, asm
        else:
            assert "rdhwr" in asm, asm
for elf in elfs:
    disasm = subprocess.check_output([str(objdump), "-d", str(elf)], text=True)
    count = 0
    for line in disasm.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s", line)
        if not match:
            continue
        count += 1
        word = int(match[2], 16)
        assert word != 0x7C03E83B, f"{elf}: RDHWR/SQ collision: " + line
        assert word >> 26 not in (0x30, 0x34, 0x38, 0x3C), f"{elf}: LL/SC: " + line
    assert count > 1000, f"missing or unexpectedly small disassembly: {elf}"
    print(f"{elf.name}: R5900 ISA checks passed ({count} instructions)")
print("R5900 TLS/generic MIPS codegen checks passed")
