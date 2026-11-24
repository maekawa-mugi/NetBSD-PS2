# Building NetBSD/playstation2

This tree builds the PlayStation 2 port with the standard `build.sh`.
`MACHINE=playstation2` uses `MACHINE_ARCH=mips64el`: the kernel and the
whole userland are n32 code for the Emotion Engine (R5900), soft-float,
without LL/SC. The `build.sh tools` cross compiler is configured with
`--with-arch=r5900 --with-llsc=no`, so the kernel, userland, X and
third-party code need no extra compiler flags.

## Requirements

- A host that can run `build.sh` (NetBSD, Linux, macOS, ...): C/C++
  compiler, make, zlib headers. Nothing outside this source tree is needed
  for the kernel and base system. On Linux hosts where `cc` is clang, the
  host build of mpfr fails to configure against glibc's `_Float128`
  headers; use GCC for the host tools: `HOST_CC=gcc HOST_CXX=g++`.
- For X11 only: a NetBSD `xsrc` checkout from the same date as this tree
  (NetBSD-current of 2026-10-15; a release xsrc such as 11.0 does not
  match), passed with `build.sh -x -X /path/to/xsrc` as for any port:

      git clone https://github.com/NetBSD/xsrc.git
      git -C xsrc checkout $(git -C xsrc rev-list -1 --before=2026-10-15T08:00Z HEAD)

  The PS2 pixman backend (R5900 MMI) is the git submodule
  `external/mit/pixman-retro/dist`: clone with `--recurse-submodules` or
  run `git submodule update --init`. xsrc is used unmodified.
- For `build-ps2-ports.sh` only: `curl`, GNU make, autoconf/automake,
  libtool and pkg-config on the host.
- For regenerating the IOP modules only (not needed to build): a ps2dev/
  PS2SDK install; see `sys/arch/playstation2/iop/README.md`.

## With build.sh

    ./build.sh -U -m playstation2 -O ../obj tools
    ./build.sh -U -m playstation2 -O ../obj release
    ./build.sh -U -m playstation2 -O ../obj live-image

Add `-x -X /path/to/xsrc` to all three for X11. Results under
`RELEASEDIR` (`../obj/releasedir` by default):

| Path | Contents |
|---|---|
| `playstation2/binary/kernel/netbsd-PS2WSCONS.gz` | kernel for 128 MiB (PS2 ELF, gzip'd) |
| `playstation2/binary/kernel/netbsd-PS2WSCONS32.gz` | the same for 32 MiB |
| `playstation2/binary/sets/*.tgz` | binary sets |
| `images/NetBSD-*-playstation2-live.img.gz` | HDD image (root `wd0a`, swap `wd0b`) |

Other kernels: `./build.sh ... kernel=PS2HDD` etc.; the configurations
are in `sys/arch/playstation2/conf` (`PS2WSCONS` is the full one: GS
wsdisplay, USB keyboard/mouse, DEV9 Ethernet and HDD, root on `wd0a`).
`PS2WSCONS32` is `PS2WSCONS` with `PS2_MEMSIZE=32` and without the
16 MiB embedded RAM disk that the bring-up configurations carry; the
release builds both (`KERNEL_SETS` in `etc/etc.playstation2/Makefile.inc`)
and the userland and live image are shared.

## With build-ps2.sh

`build-ps2.sh` runs the same `build.sh` commands with fixed output
directories under `build/ps2/` (see `build-ps2.conf`; every path can be
overridden from the environment), which the other helper scripts read:

    sh build-ps2.sh                          # tools release live-image
    X=yes XSRC=/path/to/xsrc sh build-ps2.sh # the same with X11
    sh build-ps2.sh kernel=PS2HDD            # any other build.sh operation
    sh build-ps2-ports.sh jpeg png fltk dillo  # optional, needs X

The ports are installed under `/usr/pkg` in `DESTDIR`; they are not part of
the sets or the live image.

## Booting

- Kernel: uncompress `netbsd-PS2WSCONS.gz` (128 MiB of EE RAM; in PCSX2
  enable the extra memory option) or `netbsd-PS2WSCONS32.gz` (32 MiB,
  extra memory off) and start the ELF directly (PCSX2 "Boot ELF", or an
  ELF loader on hardware). No SBIOS/RTE is used. Console: EE SIO and the
  GS screen.
- In PCSX2 both reach a multi-user login and run Xorg. With 32 MiB about
  29 MB is available after the kernel and a few X clients push about
  20 MB to swap, yet starting twm, xclock and xterm took the same time
  (about 145 s) as with 128 MiB.
- Root: write the uncompressed live image to the DEV9 HDD (for PCSX2, use
  it as the HDD image file while PCSX2 is stopped). It carries a BSD
  disklabel in sector 1 with `wd0a` as root and `wd0b` as swap, and
  enables DHCP on `smap0`.

## Tests

`tests/ps2/` holds host-side unit tests (`*_host_test.py`) and builders
for small RAM-root kernels used during bring-up; they use the outputs of
`build-ps2.sh`:

    python3 tests/ps2/iop_bootstrap_host_test.py
    python3 tests/ps2/userland_elf_test.py  # scan DESTDIR for non-R5900 opcodes
    sh tests/ps2/build-smoke-root.sh    # ELFROOT with a minimal init
    sh tests/ps2/build-shell-root.sh    # PS2SHELL with a static sh, ls, ...
    sh tests/ps2/build-hdd-regress.sh   # PS2HDD + hddprobe
    sh tests/ps2/build-net-regress.sh   # PS2NET + netprobe/jit-regress
    sh tests/ps2/build-userland-check.sh

The kernels with embedded roots are written to `build/ps2/tests/`.
`doc/ps2-elfboot.md` is the chronological bring-up log.
