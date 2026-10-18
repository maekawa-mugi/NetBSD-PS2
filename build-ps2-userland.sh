#!/bin/sh
# Run inside WSL. Build a fuller static n32 userland tree and a root FFS image.
#   sh build-ps2-userland.sh        build programs, assemble build/ps2/userland/tree,
#                                   write userland-root.ffs (partition a of the HDD)
# Prerequisite: build-ps2-user.sh {toolchain,headers,libs,libs2}.  Programs that
# fail to build are reported and skipped; this never touches the kernel build.
set -eu
cd "$(dirname "$0")"
src=$(pwd)
user="$src/build/ps2/user-n32"
tools="$src/build/ps2/tools/bin"
out="$src/build/ps2/userland"
tree="$out/tree"
size=${ROOT_SIZE:-192m}
mkdir -p "$user/logs" "$out"

# srcdir[:progname]  -> installed under dir given by the group header.
bin_progs="bin/cat bin/chmod bin/cp bin/csh bin/date bin/dd bin/df bin/echo bin/ed bin/expr
    bin/hostname bin/kill bin/ksh bin/ln bin/ls bin/mkdir bin/mv bin/pax bin/ps bin/pwd bin/rmdir
    bin/sleep bin/stty bin/sync bin/test bin/rm bin/sh"
sbin_progs="sbin/init sbin/mount sbin/umount sbin/mount_ffs sbin/mount_tmpfs sbin/fsck sbin/fsck_ffs
    sbin/newfs sbin/disklabel sbin/fdisk sbin/ifconfig sbin/route sbin/ping sbin/reboot sbin/shutdown
    sbin/sysctl sbin/dmesg sbin/swapctl sbin/mknod sbin/chown sbin/rcorder sbin/ldconfig sbin/dump"
usrbin_progs="usr.bin/basename usr.bin/cal usr.bin/cksum usr.bin/cmp usr.bin/cut usr.bin/diff
    usr.bin/dirname usr.bin/env usr.bin/false usr.bin/find usr.bin/fstat usr.bin/head
    usr.bin/hexdump usr.bin/id usr.bin/kdump usr.bin/ktrace usr.bin/ldd usr.bin/logger usr.bin/login
    usr.bin/make usr.bin/mktemp usr.bin/nc usr.bin/netstat usr.bin/nohup usr.bin/passwd
    usr.bin/patch usr.bin/printf usr.bin/sed usr.bin/seq usr.bin/sort usr.bin/stat usr.bin/su
    usr.bin/tail usr.bin/tee usr.bin/touch usr.bin/tr usr.bin/true usr.bin/tset usr.bin/tty
    usr.bin/uname usr.bin/uniq usr.bin/vmstat usr.bin/w usr.bin/wc usr.bin/who usr.bin/xargs usr.bin/yes
    usr.bin/grep usr.bin/xinstall usr.bin/getconf external/historical/nawk/bin
    external/bsd/less/bin/less external/bsd/nvi/usr.bin/nvi"
usrsbin_progs="usr.sbin/arp usr.sbin/dumpfs usr.sbin/pwd_mkdb usr.sbin/vipw"
libexec_progs="libexec/getty libexec/ld.elf_so"

build_one()
{
    dir=$1
    name=$(printf '%s' "$dir" | tr / _)
    log="$user/logs/u-$name.log"
    sh ./build-ps2-user.sh make -C "$dir" obj >"$log" 2>&1 || true
    if [ "${NOBUILD:-no}" = yes ]; then return 0; fi
    sh ./build-ps2-user.sh make -C "$dir" LDSTATIC=-static dependall >>"$log" 2>&1 || {
        echo "skip (build failed): $dir" >&2; return 1; }
}

objpath()
{
    # program binary for source dir $1 (nawk/nvi/less keep their own names)
    d="$user/obj/$1"
    case $1 in
    external/historical/nawk/bin) d="$user/obj/$1"; f=awk ;;
    external/bsd/less/bin/less) f=less ;;
    external/bsd/nvi/usr.bin/nvi) f=vi ;;
    *) f=$(basename "$1") ;;
    esac
    [ -f "$d/$f" ] && echo "$d/$f"
}

rm -rf "$tree"
mkdir -p "$tree/bin" "$tree/sbin" "$tree/usr/bin" "$tree/usr/sbin" "$tree/usr/libexec" \
    "$tree/usr/share/misc" "$tree/dev" "$tree/etc" "$tree/root" "$tree/tmp" "$tree/var/run" \
    "$tree/var/log" "$tree/var/tmp" "$tree/var/db" "$tree/mnt" "$tree/home" "$tree/libexec"

install_group()
{
    dest=$1; shift
    for dir in "$@"; do
        build_one "$dir" || continue
        p=$(objpath "$dir") || { echo "skip (no binary): $dir" >&2; continue; }
        cp "$p" "$tree/$dest/$(basename "$p")"
    done
}
install_group bin $bin_progs
install_group sbin $sbin_progs
install_group usr/bin $usrbin_progs
install_group usr/sbin $usrsbin_progs
install_group usr/libexec libexec/getty
install_group libexec libexec/ld.elf_so

# Dynamic runtime (built by PIC=yes build-ps2-user.sh), plus the on-target checks.
pic="$src/build/ps2/user-n32-pic"
mkdir -p "$tree/usr/lib" "$tree/lib" "$tree/usr/libexec" "$tree/libexec"
if [ -f "$pic/dest/libexec/ld.elf_so" ]; then
    cp "$pic/dest/libexec/ld.elf_so" "$tree/usr/libexec/ld.elf_so"
    cp "$pic/dest/libexec/ld.elf_so" "$tree/libexec/ld.elf_so"
    for so in "$pic"/dest/usr/lib/lib*.so.*.*; do
        [ -f "$so" ] || continue
        b=$(basename "$so"); major=$(printf '%s' "$b" | sed 's/\(\.so\.[0-9]*\).*/\1/')
        cp "$so" "$tree/usr/lib/$b"; cp "$so" "$tree/lib/$b"
        ln -sf "$b" "$tree/usr/lib/$major"; ln -sf "$b" "$tree/lib/$major"
    done
    [ -f build/ps2/userland-test/uc-dyn ] && cp build/ps2/userland-test/uc-dyn "$tree/usr/bin/uc-dyn"
fi
for t in uc-st ldbl-check; do
    [ -f build/ps2/userland-test/$t ] && cp build/ps2/userland-test/$t "$tree/usr/bin/$t"
done
# test binary [ is the same program as test
[ -f "$tree/bin/test" ] && ln -f "$tree/bin/test" "$tree/bin/["
for l in halt poweroff; do ln -sf reboot "$tree/sbin/$l"; done


"$tools/mipsel--netbsd-strip" "$tree"/bin/* "$tree"/sbin/* "$tree"/usr/bin/* \
    "$tree"/usr/sbin/* "$tree"/usr/libexec/* "$tree"/libexec/* 2>/dev/null || true
chmod 4555 "$tree/usr/bin/su" "$tree/usr/bin/passwd" 2>/dev/null || true

sh ./tests/ps2/userland-etc.sh "$tree" "$tools"

# mtree spec: all owned by root, plus device nodes.
spec="$out/root.mtree"
{
    python3 tests/ps2/tree2mtree.py "$tree"
    cat <<'EOF'
./dev/console type=char mode=0600 device=native,2,0
./dev/eesio type=char mode=0600 device=native,9,0
./dev/tty type=char mode=0666 device=native,3,0
./dev/null type=char mode=0666 device=native,0,2
./dev/zero type=char mode=0666 device=native,0,12
./dev/wd0a type=block mode=0640 device=native,1,0
./dev/wd0b type=block mode=0640 device=native,1,1
./dev/wd0c type=block mode=0640 device=native,1,2
./dev/rwd0a type=char mode=0640 device=native,10,0
./dev/rwd0b type=char mode=0640 device=native,10,1
./dev/rwd0c type=char mode=0640 device=native,10,2
./dev/wd1b type=block mode=0640 device=native,1,17
./dev/rwd1b type=char mode=0640 device=native,10,17
EOF
} >"$spec"
rm -f "$spec.body"

"$tools/nbmakefs" -B le -t ffs -o version=1 -s "$size" -F "$spec" "$out/userland-root.ffs" "$tree"
du -sh "$tree" | sed 's/^/tree size: /'
ls -l "$out/userland-root.ffs"
