NetBSD/playstation2
===================

This repository is NetBSD-current (src, as of 2026-10-15) with a revived
and extended port to the Sony PlayStation 2 (`MACHINE=playstation2`).
It builds with the standard `build.sh`, like any other NetBSD port.

What the port does today:

- Boots the kernel directly as a PS2 ELF, without the Linux-kit SBIOS/RTE.
- Brings up the IOP with its own BSD-licensed resident modules
  (bootstrap, USB and DEV9 interrupt relays) and talks to it over a
  native SIF implementation.
- Drives the USB host controller (OHCI) for keyboards and mice, the DEV9
  Ethernet adapter (`smap0`) and the internal ATA HDD (`wd0`), and runs
  the root file system from the HDD.
- Provides a GS console and framebuffer (`ps2fb`, wsdisplay), so X11
  (Xorg with the wsfb driver) runs on the GS.
- Runs a full NetBSD userland built as n32 code for the Emotion Engine
  (R5900): soft-float, no LL/SC, no 64-bit multiply/divide instructions.

Status: developed and tested in the PCSX2 emulator only. It boots to a
multi-user login from the HDD image, gets an address by DHCP, reaches the
Internet, and starts Xorg (640x480, 8 bpp). Real hardware is untested.
The kernel expects 128 MiB of EE RAM, which PCSX2 provides with its
extra-memory option; a retail console has 32 MiB.


Getting the source
------------------

The PS2 pixman backend is a git submodule, so clone with
`--recurse-submodules`:

    git clone --recurse-submodules https://github.com/maekawa-mugi/NetBSD-PS2.git

In an existing clone, or after pulling a change to the submodule:

    git submodule update --init

The submodule (`external/mit/pixman-retro/dist`) is only needed for X11.
Without it, the X11 build stops with a message telling you to run the
command above.


Requirements
------------

- A host that can run `build.sh` (NetBSD, Linux, macOS, ...) with a C and
  C++ compiler, make and zlib headers. The kernel and base system need
  nothing outside this repository.
- On Linux hosts where `cc` is clang, the host build of mpfr fails to
  configure against glibc's `_Float128` headers. Build the host tools with
  GCC: `HOST_CC=gcc HOST_CXX=g++`.
- For X11: a NetBSD `xsrc` tree from the same date as this source:

      git clone https://github.com/NetBSD/xsrc.git
      git -C xsrc checkout $(git -C xsrc rev-list -1 --before=2026-10-15T08:00Z HEAD)

  A release xsrc such as NetBSD 11.0 does not match and will fail. xsrc
  is used unmodified.
- About 6 GB of disk space for the build output with X11, in addition to
  the sources.


Building
--------

### Quick start

`build-ps2.sh` runs `build.sh -m playstation2` with fixed output
directories under `build/ps2/` and ignores the host's `/etc/mk.conf`:

    sh build-ps2.sh                                   # without X11
    X=yes XSRC=/path/to/xsrc sh build-ps2.sh          # with X11

By default it runs the `tools`, `release` and `live-image` operations.
Any other `build.sh` operation can be given instead, for example
`sh build-ps2.sh kernel=PS2HDD`. Output directories and the job count can
be changed with `BUILDTOP`, `OBJDIR`, `TOOLDIR`, `DESTDIR`, `RELEASEDIR`
and `JOBS` (see `build-ps2.conf`).

### With build.sh directly

    ./build.sh -U -m playstation2 -O ../obj tools
    ./build.sh -U -m playstation2 -O ../obj release
    ./build.sh -U -m playstation2 -O ../obj live-image

Add `-x -X /path/to/xsrc` to each command for X11.

### Results

Paths are relative to the release directory (`build/ps2/release` with
`build-ps2.sh`, `../obj/releasedir` with the commands above):

| Path | Contents |
|---|---|
| `playstation2/binary/kernel/netbsd-PS2WSCONS.gz` | kernel, a PS2 ELF (gzip'd) |
| `playstation2/binary/sets/*.tgz` | binary sets |
| `images/NetBSD-*-playstation2-live.img.gz` | HDD image with the installed system |

The live image holds the `base`, `etc`, `misc`, `rescue` and `text` sets,
plus the X11 sets when X11 was built. It has a BSD disklabel in sector 1
with the root file system on `wd0a` and swap on `wd0b`, and configures
`smap0` by DHCP. The compiler set (`comp`) is not included.

### Kernel configurations

The configurations are in `sys/arch/playstation2/conf`. `PS2WSCONS` is
the full one (GS wsdisplay, USB keyboard and mouse, DEV9 Ethernet and
HDD, root on `wd0a`) and is the one shipped in the release. The others
are smaller steps used during bring-up, such as `PS2SHELL` (RAM-disk
root) and `PS2HDD`.


Running in PCSX2
----------------

1. Uncompress the kernel: `gzip -d netbsd-PS2WSCONS.gz`.
2. Uncompress the live image and use it as the DEV9 HDD image file
   (`[DEV9/Hdd]` `HddFile` in `PCSX2.ini`, with `HddEnable = true`).
   Only change the file while PCSX2 is stopped.
3. In the PCSX2 settings, enable full TLB emulation and the extra memory
   option (128 MiB).
4. For networking, enable the DEV9 Ethernet adapter (for example the
   Sockets API with DHCP interception).
5. For input, attach a USB keyboard and, for X11, a USB mouse.
6. Start the kernel ELF directly (boot ELF). The console appears on the
   EE serial port and on the GS screen; log in as `root` (no password).

Run `startx` or `X` as root for X11 on the GS.


Optional third-party programs
-----------------------------

`build-ps2-ports.sh` cross-builds a few programs that are not part of
NetBSD against the build output, after an X11 build:

    sh build-ps2-ports.sh jpeg png fltk dillo

It downloads the source archives and checks them against SHA512 sums kept
in the script. The programs are installed under `/usr/pkg` in `DESTDIR`;
they are not part of the sets or the live image.


Tests
-----

`tests/ps2/` holds host-side unit tests and builders for small test
kernels with embedded RAM-disk roots. They use the output of
`build-ps2.sh`:

    python3 tests/ps2/userland_elf_test.py    # scan DESTDIR for opcodes the R5900 lacks
    python3 tests/ps2/iop_bootstrap_host_test.py
    sh tests/ps2/build-smoke-root.sh          # ELFROOT with a minimal init
    sh tests/ps2/build-shell-root.sh          # PS2SHELL with a static shell
    sh tests/ps2/build-hdd-regress.sh         # PS2HDD with hddprobe
    sh tests/ps2/build-net-regress.sh         # PS2NET with netprobe

The test kernels are written to `build/ps2/tests/`.


Where things are
----------------

| Path | Contents |
|---|---|
| `sys/arch/playstation2/` | the port: machine code, drivers, kernel configurations |
| `sys/arch/playstation2/iop/` | IOP modules and their generated images (`mkimages.sh`) |
| `distrib/playstation2/liveimage/` | live HDD image definition |
| `external/mit/pixman-retro/` | pixman with the R5900 MMI backend (submodule) |
| `tests/ps2/` | PS2 test programs and builders |
| `doc/ps2-build.md` | build notes in more detail |
| `doc/ps2-elfboot.md` | chronological bring-up log |


Licences
--------

NetBSD is distributed under the BSD licence; see the copyright notices in
the source files. The IOP modules in `sys/arch/playstation2/iop` are
BSD-2-Clause and contain no PS2SDK code. pixman-retro is under the MIT
licence.


About NetBSD
------------

NetBSD is a free, fast, secure, and highly portable Unix-like Open Source
operating system. For NetBSD itself, see:

- [NetBSD](https://www.NetBSD.org/) and the
  [list of ports](https://wiki.NetBSD.org/ports/)
- [BUILDING](BUILDING) for `build.sh` in general
- [The NetBSD Guide](https://www.NetBSD.org/docs/guide/en/) and the
  [manual pages](https://man.NetBSD.org/)
- The upstream sources: `git clone https://github.com/NetBSD/src.git`

Problems with the PlayStation 2 port belong in this repository, not in
the NetBSD bug database.
