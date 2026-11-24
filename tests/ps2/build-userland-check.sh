#!/bin/sh
# On-target userland checks as dynamic and static binaries in
# $BUILDTOP/tests/userland-check; copy them onto a running system to run.
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
out="$TOUT/userland-check"
mkdir -p "$out"
$CC -O2 -Wall -Wextra -o "$out/uc-dyn" tests/ps2/userland-check.c -lpthread -lm
$CC -static -O2 -Wall -Wextra -o "$out/uc-st" tests/ps2/userland-check.c -lpthread -lm
$CC -static -O2 -Wall -o "$out/ldbl-check" tests/ps2/ldbl-check.c -lm
ls -l "$out"
