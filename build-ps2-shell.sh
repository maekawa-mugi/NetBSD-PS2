#!/bin/sh
# Run inside WSL. Static n32 userland and a 12 MiB writable FFS RAM root.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
user="$src/build/ps2/user-n32"
tools="$src/build/ps2/tools/bin"
out="$src/build/ps2/root-shell"
kernel=${KERNEL:-PS2SHELL}
# Prerequisite: sh build-ps2-user.sh toolchain; headers; libs.
sh ./build-ps2-user.sh make -C lib/libutil obj >"$user/logs/shell-libutil.log" 2>&1
sh ./build-ps2-user.sh make -C lib/libutil dependall >>"$user/logs/shell-libutil.log" 2>&1
sh ./build-ps2-user.sh make -C lib/libutil MKUPDATE=no install >>"$user/logs/shell-libutil.log" 2>&1
for dir in bin/sh bin/cat bin/mkdir bin/rm bin/ls usr.bin/uname; do
    name=$(basename "$dir")
    sh ./build-ps2-user.sh make -C "$dir" obj >"$user/logs/shell-$name.log" 2>&1
    if [ "$name" = sh ]; then
        sh ./build-ps2-user.sh make -C "$dir" SMALLPROG=1 LDADD= DPADD= LDSTATIC=-static dependall >>"$user/logs/shell-$name.log" 2>&1
    else
        sh ./build-ps2-user.sh make -C "$dir" LDSTATIC=-static dependall >>"$user/logs/shell-$name.log" 2>&1
    fi
done
mkdir -p "$out/tree/bin" "$out/tree/sbin" "$out/tree/dev" \
    "$out/tree/etc" "$out/tree/root" "$out/tree/tmp"
for prog in sh cat mkdir rm ls; do
    cp "$user/obj/bin/$prog/$prog" "$out/tree/bin/$prog"
done
cp "$user/obj/usr.bin/uname/uname" "$out/tree/bin/uname"
for prog in init-shell ramrootctl; do
    "$user/cc" --sysroot="$user/dest" -O2 -Wall -Wextra -Werror \
        -c "tests/ps2/$prog.c" -o "$out/$prog.o"
    "$user/cc" --sysroot="$user/dest" -static -nostdlib -o "$out/$prog" \
        "$user/dest/usr/lib/crt0.o" "$user/dest/usr/lib/crti.o" \
        "$user/dest/usr/lib/crtbegin.o" "$out/$prog.o" \
        -Wl,--start-group "$user/dest/usr/lib/libc.a" \
        "$user/dest/usr/lib/libgcc.a" -Wl,--end-group \
        "$user/dest/usr/lib/crtend.o" "$user/dest/usr/lib/crtn.o"
done
cp "$out/init-shell" "$out/tree/sbin/init"
cp "$out/ramrootctl" "$out/tree/sbin/halt"
"$tools/mipsel--netbsd-strip" "$out/tree/bin/"* "$out/tree/sbin/init" "$out/tree/sbin/halt"
ln -sf halt "$out/tree/sbin/reboot"
ln -sf halt "$out/tree/sbin/sync"
printf 'root:*:0:0:Charlie &:/root:/bin/sh\n' >"$out/tree/etc/passwd"
printf 'wheel:*:0:root\n' >"$out/tree/etc/group"
cat >"$out/root.mtree" <<'EOF'
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
EOF
"$tools/nbmakefs" -B le -t ffs -o version=1 -s 12m \
    -F "$out/root.mtree" "$out/root.fs" "$out/tree"
KERNEL="$kernel" sh ./build-ps2.sh
"$tools/mipsel--netbsd-mdsetimage" -v "$src/build/ps2/netbsd-$kernel.elf" "$out/root.fs"
