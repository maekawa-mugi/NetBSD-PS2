#!/usr/bin/env python3
"""Record production cache walker operations; does not emulate cache hardware.

Run in WSL: python3 tests/ps2/cache_r5900_host_test.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "sys/arch/mips/mips/cache_r5900.c").read_text()
source = re.sub(r"^#include .*\n", "", source, flags=re.M)
header = (root / "sys/arch/mips/include/cache_r5900.h").read_text()
constants = "\n".join(re.findall(
    r"^#define\s+(?:CACHE_R5900|CACHEOP_R5900)_.*$", header, re.M))
prototypes = header[header.index("void\tr5900_icache_sync_all_64"):]
prototypes = prototypes[:prototypes.index("#endif")]

harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t vaddr_t;
typedef uint32_t vsize_t;
typedef int64_t register_t;
#define MIPS_PHYS_TO_KSEG0(x) ((vaddr_t)(x) | 0x80000000U)
static struct { uint32_t va; int op; } operations[1024];
static unsigned count;
static void record(uint32_t va, int op) {
    assert(count < 1024);
    operations[count].va = va; operations[count++].op = op;
}
#define cache_op_r5900_line_64(va, op) record((va), (op))
'''
checks = r'''
static void expect(unsigned i, uint32_t va, int op) {
    assert(i < count);
    assert(operations[i].va == va && operations[i].op == op);
}
int main(void) {
    /* Empty, unaligned ranges must not invalidate any lines. */
    r5900_icache_sync_range_64(0x8000003f, 0);
    r5900_icache_sync_range_index_64(0x8000003f, 0);
    r5900_pdcache_inv_range_64(0x8000003f, 0);
    assert(count == 0);

    /* A two-byte span across a line boundary must visit both lines. */
    r5900_pdcache_inv_range_64(0x8000003f, 2);
    assert(count == 2);
    expect(0, 0x80000000, 0x1a); expect(1, 0x80000040, 0x1a);
    count = 0;

    /* Index operations wrap the index and explicitly select both ways. */
    r5900_pdcache_wbinv_range_index_64(0xc0000fff, 2);
    assert(count == 4);
    expect(0, 0x80000fc0, 0x14); expect(1, 0x80000fc1, 0x14);
    expect(2, 0x80000000, 0x14); expect(3, 0x80000001, 0x14);
    count = 0;

    r5900_pdcache_wbinv_all_64();
    assert(count == 128);
    for (unsigned i = 0; i < 128; i++)
        expect(i, 0x80000000 + (i / 2) * 64 + (i % 2), 0x14);
    count = 0;

    /* Data writeback precedes instruction invalidation. */
    r5900_icache_sync_range_64(0x80101000, 64);
    assert(count == 2);
    expect(0, 0x80101000, 0x1c); expect(1, 0x80101000, 0x0b);
    count = 0;

    r5900_icache_sync_all_64();
    assert(count == 128 + 256);
    for (unsigned i = 0; i < 256; i++)
        expect(128 + i, 0x80000000 + (i / 2) * 64 + (i % 2), 0x07);
    count = 0;

    /* Huge index ranges cap at a full sweep and do not overflow. */
    r5900_icache_sync_range_index_64(0xffffffc1, UINT32_MAX);
    assert(count == 384);
    assert(r5900_lines(0xffffffff, UINT32_MAX) == 67108865ULL);
    puts("R5900 cache range/way/order checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix="r5900-cache-") as tmp:
    path = Path(tmp)
    (path / "check.c").write_text("\n".join(
        [harness, constants, prototypes, source, checks]))
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    str(path / "check.c"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
