#!/bin/sh
# Shell test root: PS2SHELL kernel (or KERNEL=...) with a 12 MiB writable
# FFS md root: static sh/cat/mkdir/rm/ls/uname built from this tree with
# build.sh's nbmake wrapper, init-shell as /sbin/init and ramrootctl as
# halt/reboot/sync.  Other test roots start from this tree.
#   sh tests/ps2/build-shell-root.sh   -> $BUILDTOP/tests/netbsd-PS2SHELL-shell.elf
set -eu
cd "$(dirname "$0")/../.."
. tests/ps2/env.sh
kernel=${KERNEL:-PS2SHELL}
out="$TOUT/root-shell"
[ -f "$DESTDIR/usr/lib/libc.a" ] || { echo "no userland in $DESTDIR: run build-ps2.sh first" >&2; exit 1; }
need_kernel "$kernel"
rm -rf "$out"; mkdir -p "$out/tree/bin" "$out/tree/sbin" "$out/tree/dev" \
    "$out/tree/etc" "$out/tree/root" "$out/tree/tmp"
# Static, small programs in a separate objdir (the release objects are dynamic).
for dir in bin/sh bin/cat bin/mkdir bin/rm bin/ls usr.bin/uname; do
    prog=$(basename "$dir")
    objdir="$TOUT/obj/$dir"
    extra=
    [ "$prog" = sh ] && extra="SMALLPROG=1 LDADD= DPADD="
    { tmake -C "$dir" obj && tmake -C "$dir" DESTDIR="$DESTDIR" \
        LDSTATIC=-static MKMAN=no $extra dependall; } \
        >"$TOUT/static-$prog.log" 2>&1 || { echo "build failed: $dir ($TOUT/static-$prog.log)" >&2; exit 1; }
    cp "$objdir/$prog" "$out/tree/bin/$prog"
    "$STRIP" "$out/tree/bin/$prog"
done
ccstatic "$out/tree/sbin/init" tests/ps2/init-shell.c
ccstatic "$out/tree/sbin/halt" tests/ps2/ramrootctl.c
ln -sf halt "$out/tree/sbin/reboot"
ln -sf halt "$out/tree/sbin/sync"
printf 'root:*:0:0:Charlie &:/root:/bin/sh\n' >"$out/tree/etc/passwd"
printf 'wheel:*:0:root\n' >"$out/tree/etc/group"
cat >"$out/root.mtree" <<'X'
/set uid=0 gid=0
. type=dir mode=0755
./bin type=dir mode=0755
./bin/sh type=file mode=0555
./bin/cat type=file mode=0555
./bin/mkdir type=file mode=0555
./bin/rm type=file mode=0555
./bin/ls type=file mode=0555
./bin/uname type=file mode=0555
./dev type=dir mode=0755
./dev/console type=char mode=0600 device=native,2,0
./dev/eesio type=char mode=0600 device=native,9,0
./dev/tty type=char mode=0666 device=native,3,0
./dev/null type=char mode=0666 device=native,0,2
./dev/zero type=char mode=0666 device=native,0,12
./etc type=dir mode=0755
./etc/passwd type=file mode=0644
./etc/group type=file mode=0644
./root type=dir mode=0700
./tmp type=dir mode=01777
./sbin type=dir mode=0755
./sbin/init type=file mode=0555
./sbin/halt type=file mode=0555
./sbin/reboot type=link link=halt
./sbin/sync type=link link=halt
X
"$MAKEFS" -B le -t ffs -o version=1 -s 12m -F "$out/root.mtree" "$out/root.fs" "$out/tree"
embed_root "$kernel" "$out/root.fs" "$kernel-shell"
