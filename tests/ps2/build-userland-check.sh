#!/bin/sh
# Build the on-target check as static and dynamic n32 binaries into build/ps2/userland-test.
set -eu
cd "$(dirname "$0")/../.."
o=build/ps2/userland-test
mkdir -p "$o"
d=build/ps2/user-n32-pic
s=build/ps2/user-n32
$d/cc --sysroot=$d/dest -O2 -Wall -Wextra -c tests/ps2/userland-check.c -o $o/uc-dyn.o
$d/cc --sysroot=$d/dest -o $o/uc-dyn $o/uc-dyn.o $d/dest/usr/lib/sysident.o -lpthread -lm
$s/cc --sysroot=$s/dest -O2 -Wall -Wextra -c tests/ps2/userland-check.c -o $o/uc-st.o
$s/cc --sysroot=$s/dest -static -nostdlib -o $o/uc-st \
    $s/dest/usr/lib/crt0.o $s/dest/usr/lib/crti.o $s/dest/usr/lib/crtbegin.o $o/uc-st.o \
    -Wl,--start-group $s/dest/usr/lib/libpthread.a $s/dest/usr/lib/libm.a \
    $s/dest/usr/lib/libc.a $s/dest/usr/lib/libgcc.a -Wl,--end-group \
    $s/dest/usr/lib/crtend.o $s/dest/usr/lib/crtn.o
for t in ldbl-check; do
    $s/cc --sysroot=$s/dest -O2 -Wall -c tests/ps2/$t.c -o $o/$t.o
    $s/cc --sysroot=$s/dest -static -nostdlib -o $o/$t \
        $s/dest/usr/lib/crt0.o $s/dest/usr/lib/crti.o $s/dest/usr/lib/crtbegin.o $o/$t.o \
        -Wl,--start-group $s/dest/usr/lib/libm.a $s/dest/usr/lib/libc.a $s/dest/usr/lib/libgcc.a -Wl,--end-group \
        $s/dest/usr/lib/crtend.o $s/dest/usr/lib/crtn.o
done
