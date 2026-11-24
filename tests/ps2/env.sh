# Sourced by tests/ps2/build-*.sh after `cd` to the source top.
# Paths come from build-ps2.conf (the build.sh output of build-ps2.sh);
# the mips64el toolchain already defaults to R5900/n32/soft-float.
src=$(pwd)
. ./build-ps2.conf
tb="$TOOLDIR/bin"
CC="$tb/mips64el--netbsd-gcc --sysroot=$DESTDIR"
STRIP="$tb/mips64el--netbsd-strip"
MAKEFS="$tb/nbmakefs"
MDSETIMAGE="$tb/mips64el--netbsd-mdsetimage"
KOBJ="$OBJDIR/sys/arch/playstation2/compile"
TOUT="$BUILDTOP/tests"
[ -x "$tb/mips64el--netbsd-gcc" ] || { echo "no toolchain in $tb: run build-ps2.sh tools" >&2; exit 1; }
mkdir -p "$TOUT"

# kernel CONFIG: build it with build.sh unless already built
need_kernel()
{
    [ -f "$KOBJ/$1/netbsd" ] || sh ./build-ps2.sh "kernel=$1"
}

# ccstatic out src...: static target binary from C/asm sources
ccstatic()
{
    _cs_out=$1; shift
    $CC -static -O2 -Wall -Wextra -Werror -o "$_cs_out" "$@"
    "$STRIP" "$_cs_out"
}

# embed_root CONFIG fs name: copy the kernel to $TOUT/netbsd-name.elf and
# store the FFS image in its md root
embed_root()
{
    cp "$KOBJ/$1/netbsd" "$TOUT/netbsd-$3.elf"
    "$MDSETIMAGE" -v "$TOUT/netbsd-$3.elf" "$2"
}

# tmake args...: build.sh's nbmake wrapper, but with objects under
# $TOUT/obj so test-only (e.g. static) builds never touch the release objdir
tmake()
{
    _tm_w="$TOUT/nbmake-tests"
    sed "s|^MAKEOBJDIR=.*|MAKEOBJDIR='\${.CURDIR:C,^$src,$TOUT/obj,}'; export MAKEOBJDIR|" \
        "$tb/nbmake-playstation2" >"$_tm_w"
    sh "$_tm_w" "$@"
}
