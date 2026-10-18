#!/usr/bin/env python3
"""usage: tree2mtree.py <tree>  -- print a flat root-owned mtree spec for makefs."""
import os, stat, sys
root = sys.argv[1]
print('/set uid=0 gid=0')
print('. type=dir mode=0755')
for d, dirs, files in os.walk(root):
    dirs.sort()
    for n in sorted(dirs + files):
        p = os.path.join(d, n)
        rel = './' + os.path.relpath(p, root)
        st = os.lstat(p)
        m = stat.S_IMODE(st.st_mode)
        if stat.S_ISLNK(st.st_mode):
            print(f'{rel} type=link link={os.readlink(p)}')
        elif stat.S_ISDIR(st.st_mode):
            print(f'{rel} type=dir mode={m:04o}')
        else:
            print(f'{rel} type=file mode={m:04o}')
