#!/usr/bin/env python3
"""Write a BSD disklabel (sector 1) and an FFS image (partition a) into a raw HDD image.

usage: make-hdd-root.py <disk.raw> <ffs.img> [start_sector]
Offsets follow struct disklabel in sys/sys/disklabel.h (little-endian, 16 partitions).
"""
import struct, sys

SECSZ, SPT, HEADS = 512, 63, 16
disk, ffs = sys.argv[1], sys.argv[2]
start = int(sys.argv[3]) if len(sys.argv) > 3 else 64

import os
disk_sectors = os.path.getsize(disk) // SECSZ
ffs_sectors = os.path.getsize(ffs) // SECSZ
spc = SPT * HEADS
cyls = disk_sectors // spc
assert start + ffs_sectors <= disk_sectors

lab = bytearray(148 + 16 * 3)
def w16(o, v): lab[o:o+2] = struct.pack('<H', v)
def w32(o, v): lab[o:o+4] = struct.pack('<I', v)
w32(0, 0x82564557)
lab[8:8+9] = b'pcsx2-hdd'
lab[24:24+4] = b'root'
w32(40, SECSZ); w32(44, SPT); w32(48, HEADS); w32(52, cyls); w32(56, spc)
w32(60, disk_sectors)
w16(72, 3600); w16(74, 1)
w32(132, 0x82564557)
w16(138, 3)
w32(140, 8192); w32(144, 8192)
def part(i, size, off, fstype, frag=8, cpg=0):
    o = 148 + 16 * i
    w32(o, size); w32(o + 4, off)
    lab[o + 12] = fstype   # p_fstype
    lab[o + 13] = frag     # p_frag
    w16(o + 14, cpg)       # p_cpg
part(0, ffs_sectors, start, 7, 8, 0)   # a: FS_BSDFFS=7
part(1, 0, 0, 0)                       # b: unused
part(2, disk_sectors, 0, 0)            # c: whole disk (RAW_PART)
ck = 0
for i in range(0, len(lab), 2):
    ck ^= struct.unpack_from('<H', lab, i)[0]
w16(136, ck)

with open(disk, 'r+b') as f:
    f.seek(0); f.write(b'\0' * SECSZ)            # no stale MBR
    f.seek(SECSZ); f.write(lab)
    f.seek(start * SECSZ)
    with open(ffs, 'rb') as g:
        while True:
            b = g.read(1 << 20)
            if not b: break
            f.write(b)
print(f'label ok: a={start}+{ffs_sectors} sectors, disk={disk_sectors} sectors')
