#!/usr/bin/env python3
"""Check R5900 TLS codegen and scan target ELF files for instructions the
EE does not implement (LL/SC/LLD/SCD, DMULT/DDIV, the RDHWR/SQ collision).

usage: userland_elf_test.py [file-or-dir ...]
Defaults to the whole build.sh DESTDIR of build-ps2.sh ($DESTDIR or
build/ps2/destdir); TOOLDIR likewise ($TOOLDIR or build/ps2/tools).
"""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
buildtop = Path(os.environ.get("BUILDTOP", root / "build/ps2"))
tools = Path(os.environ.get("TOOLDIR", buildtop / "tools")) / "bin"
destdir = Path(os.environ.get("DESTDIR", buildtop / "destdir"))
cc = tools / "mips64el--netbsd-gcc"
objdump = tools / "mips64el--netbsd-objdump"

source = "__thread int value; int *get_tls(void) { return &value; }\n"
with tempfile.TemporaryDirectory(prefix="ps2-tls-") as td:
    # The toolchain defaults to R5900; -march=mips3 shows the generic path.
    for arch, flags in (("r5900", []), ("mips3", ["-march=mips3"])):
        output = Path(td) / (arch + ".s")
        subprocess.run([str(cc), *flags, "-ftls-model=local-exec", "-O2",
                        "-S", "-x", "c", "-", "-o", str(output)],
                       input=source, text=True, check=True)
        asm = output.read_text()
        if arch == "r5900":
            assert "_lwp_getprivate" in asm, asm
            assert "rdhwr" not in asm, asm
        else:
            assert "rdhwr" in asm, asm
print("R5900 TLS/generic MIPS codegen checks passed")


def elf_files(paths):
    for p in paths:
        if p.is_dir():
            for dirpath, _, names in os.walk(p):
                for n in names:
                    f = Path(dirpath) / n
                    if f.is_symlink() or not f.is_file():
                        continue
                    with open(f, "rb") as fh:
                        if fh.read(4) == b"\x7fELF":
                            yield f
        else:
            yield p


def check(elf):
    disasm = subprocess.run([str(objdump), "-d", str(elf)], text=True,
                            capture_output=True).stdout
    bad = []
    count = 0
    for line in disasm.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s", line)
        if not match:
            continue
        count += 1
        word = int(match[2], 16)
        op = word >> 26
        if word == 0x7C03E83B:
            bad.append("RDHWR/SQ collision: " + line)
        elif op in (0x30, 0x34, 0x38, 0x3C):
            bad.append("LL/SC: " + line)
        elif op == 0 and (word & 0x3F) in (0x1C, 0x1D, 0x1E, 0x1F):
            bad.append("DMULT/DDIV: " + line)
    return elf, count, bad


paths = [Path(a) for a in sys.argv[1:]] or [destdir]
failed = 0
total = 0
with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    for elf, count, bad in pool.map(check, elf_files(paths)):
        total += 1
        for b in bad[:5]:
            print(f"{elf}: {b}")
        failed += bool(bad)
assert total > 0, f"no ELF files under {paths}"
assert failed == 0, f"{failed} of {total} ELF files use instructions the R5900 lacks"
print(f"R5900 ISA checks passed ({total} ELF files)")
