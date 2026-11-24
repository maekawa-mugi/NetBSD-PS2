# PS2 direct ELF boot test (2026-10-16)

> This is the chronological bring-up log. Build commands and script names
> below are historical: the port now builds with `build.sh -m playstation2`
> (MACHINE_ARCH mips64el); see `doc/ps2-build.md`. The RAM-root test
> builders moved to `tests/ps2/build-*-root.sh`.

Current development target: SBIOS/RTE-free `PS2USB`, static n32 RAM-root
shell, direct VGA GS output and native USB keyboard input (plus EE SIO),
with **128 MiB EE RAM**.
Build with `KERNEL=PS2USB sh build-ps2-shell.sh`; PCSX2 must have
ExtraMemory=true. The stock deterministic agent profile forces 32 MiB and
is unsuitable for current 128 MiB images. Earlier sections retain the
chronological bring-up results; the final sections describe current tests.

## Build

At the time, `build-ps2.sh` ran NetBSD's own cross tools with host GCC/G++
and `MAKECONF=/dev/null`, writing `build/ps2/netbsd-ELFBOOT.elf`. See
`doc/ps2-build.md` for the current build.

The kernel uses ELF32 little-endian n32, with R5900/MIPS III flags. The
NetBSD linker needs its n32 emulation enabled; the playstation2 tools build
now enables the additional mips64el NetBSD target. Kernel linking explicitly
selects `elf32ltsmipn32`; no mismatch-suppression flag is used.

## Direct ELF entry

The kernel is linked at `0x80100000`, leaving the BIOS ELF loader's low RAM
intact. Its ELF entry is `0x00100000`, the low alias established by the BIOS.
ExecPS2 enters user mode, so a high KSEG0 entry faults before executing its
first instruction. Locore clears KSU and now explicitly jumps through a
register to its KSEG0 alias before entering C or clearing the TLB. J/JAL
alone retain the low caller address region and do not provide that switch.
This uses the ordinary BIOS ELF boot path;
it requires neither kernelreloaded nor SBIOS.

The configuration omits SBIOS/SIF/GS code and IOP devices. Its console uses
EE SIO directly. It has no root disk or userspace image.

## Initial verified result (before the cache fix)

Tested with the user-specified `pcsx2-qtx64-avx2-clang.exe`, through
`pcsx2ctl.py` and the `isolated-elf-v1-same-build` deterministic profile.
Full TLB was enabled, EE/IOP recompilers disabled, and memory was 32 MiB.
The final run advanced to absolute EE event-test index 16,000,000; the
transport reported no observation fault. The VM was then stopped normally.

EE SIO output from the kernel:

```text
[   1.0000000] kernel_text=0x80100000 edata=0x8026fe10 end=0x80278e60
[   1.0000000] panic: can't handle primary cache on impl 0x2e
```

This confirms ELF loading, execution of NetBSD's entry code, transition to
kernel mode, entry into `mach_init()`, and direct SIO output. It does not
confirm completion of kernel initialization or userspace boot. The next
known blocker is the missing R5900 case in generic cache configuration.
Cache/TLB and interrupt behavior beyond this point remains unverified.

## R5900 cache fix and subsequent verification

The EE Core User manual (chapter 6, Tables 6-1/6-2) and its CACHE instruction
reference were checked against the implementation. `cache_r5900.c` now
implements the previously declared R5900 cache API and `cache.c` selects it:

- Fixed I/D geometry: 16/8 KiB, 64-byte lines, two ways.
- Index operations sweep each way using address bit 0, with I index bits
  [12:6] and D index bits [11:6]. They use direct-mapped KSEG0 addresses.
- I invalidation uses IXIN `0x07` or IHIN `0x0b`; D operations use DXWBIN
  `0x14`, DHWBIN `0x18`, DHIN `0x1a`, and DHWOIN `0x1c`.
- Existing EE primitives issue SYNC.L/SYNC.P around the CACHE instruction,
  including SYNC.L immediately after writeback. I synchronization writes
  back D-cache before invalidating I-cache.
- Empty ranges perform no cache operations. Index ranges spanning a whole
  way use a bounded full sweep; line counting widens before addition.

The EE's 32-bit CP0 path is also selected in `mipsX_subr.S`, while retaining
64-bit GPR operations. The unreachable XTLB slot has a bounded panic stub.
Direct ELF entry explicitly moves to KSEG0 before invalidating the BIOS TLB.

Validation: kernel build/link passed without mismatch suppression; generated
CACHE encodings, synchronization and way-bit selection were inspected in
the object disassembly. `python3 tests/ps2/cache_r5900_host_test.py` passes
empty-range, boundary-crossing, index-wrap, both-way, D-before-I and large
range tests using the production range walker with recorded operations.
The host test does not model hardware cache contents.

PCSX2 runs 07 and 08 both reached the NetBSD banner, memory initialization,
and mainbus/CPU attachment. Full TLB was enabled and EE/IOP recompilers were
disabled. Run 08 additionally enabled `EnableEECache`; the saved effective
configuration confirms it. Both ran to event-test index 16,000,000 without
an observation fault and were stopped normally.

```text
total memory = 32768 KB
avail memory = 29704 KB
mainbus0 (root)
cpu0: 16KB/64B 2-way set-associative L1 instruction cache
cpu0: 8KB/64B 2-way set-associative write-back L1 data cache
```

The original cache panic is resolved. The next observed failure is a timer
interrupt entering a BIOS handler (`INTC(9) Handler does not exist`), followed
by a kernel breakpoint trap at EPC `0x80000dc0`. EE interrupt-vector routing
and the old PS2 interrupt-priority integration still require investigation.
This is not a completed userspace boot or real-hardware validation.

## Dedicated interrupt vector installed

EE Core User's Manual Table 4-1 and section 4.2.5 specify a separate
V_INTERRUPT at `0x80000200` (BEV=0). The generic MIPS3 initializer copied
only through `0x800001cf`, leaving that slot under BIOS control.

`mips3_vector_init()` now copies the existing 36-byte `mips3_intr` entry
to `MIPS3_INTR_EXC_VEC`, guarded by `MIPS3_5900`, before its existing cache
synchronization. It rejects an entry larger than the 128-byte slot. This is
a NetBSD change to guest RAM; no PCSX2 code or exception-routing behavior
was changed. The generated kernel's disassembly confirms the destination
is `0x80000200` and the copy length is derived from the entry's end symbol.

PCSX2 run 09 used Full TLB, the EE cache, and interpreters. Checkpoints at
events 16,000,000 and 18,000,000 no longer show the BIOS `INTC(9)` diagnostic
or its subsequent breakpoint panic. At the first checkpoint PC was
`0x80100998`, which resolves to NetBSD `cpu_intr()` immediately following
the call to `intc_intr()`, with Cause `0x400` (INT0 pending).

The dedicated vector therefore reaches NetBSD interrupt processing. Kernel
initialization still does not complete: the pending interrupt is not yet
drained, so PS2 INTC/DMAC masking and the generic SPL integration are the
next issue. This run was stopped normally after saving both checkpoints.

## PS2 SPL integration: root-device prompt reached

The interrupt loop was caused by the generic CP0-only SPL backend leaving
the PS2 device mask (`md_imask`) out of sync with `ci_cpl`. The pending timer
was repeatedly visible to `cpu_intr()` but excluded from its dispatcher.

The port now installs `playstation2_splsw`. Its IPL changes update both
`ci_cpl` and the INTC/DMAC masks. The pending interrupt selector reads the
raw device requests, selects the highest IPL, and passes only that IPL's
sources to the dispatcher. Registering a handler respects the current IPL.
The C interrupt handler no longer increments the interrupt depth a second
time; the assembly entry/exit already maintains it.

The mask writer follows the EE register semantics: I_MASK bits and
D_STAT.CIM bits toggle on writes of one; D_STAT's low status bits are
write-one-to-clear and are never written by mask updates. The priority
ordered handler lists also handle insertion before the previous head.

There are no EE Cause software interrupt bits. Software requests therefore
use pending flags, drained at eligible IPL drops and on kernel/user hardware
interrupt return after restoring the interrupt depth. Dispatch is deferred
inside hard interrupts, exceptions, and spin-mutex critical sections, and
is protected against recursive softint dispatch. The old one-shot softint
timer handlers and obsolete SPL assembly entry were removed.

Host tests:

```sh
python3 tests/ps2/spl_host_test.py
python3 tests/ps2/cache_r5900_host_test.py
```

The first test runs the production SPL and mask writer against modeled
toggle/W1C registers, checking priority selection, registration-level masks,
status preservation, softint priority filtering and deferred dispatch. Both
tests pass; the final kernel builds and links, and `git diff --check` passes.

Full-TLB, EE-cache-enabled interpreter run 10 reached `root device:`.
At event 18,000,000 the SIO request `?\n` was accepted at output offset 2366;
running to event 50,000,000 consumed both input bytes and produced:

```text
[   1.0698774] use one of: halt reboot
[   1.0699713] root device:
```

Final rebuilt ELF run 11 again reached the root-device prompt at event
18,000,000. Submitting `halt\n` and running to event 24,000,000 produced:

```text
[   1.0199392] root device: halt
[   1.0698774] syncing disks... done
[   1.0699891] unmounting file systems...
[   1.0699891] unmounting done
[   1.0699891] SBIOS unavailable; CPU halted.
```

No observation fault occurred. The guest consumed the submitted bytes and
performed its own halt; the emulator was subsequently stopped normally.
The final ELF SHA-256 is
`caa7e3f18c0b7595a7910bd83f35a16260979e26ddc0900ef17ee5dfe7a2f2ee`.

This verifies direct ELF entry, kernel initialization through root selection,
timer interrupt progress, polled SIO input/output, and the SBIOS-free halt
path on the specified emulator. This minimal configuration has no root
storage driver or userspace filesystem; no userspace or real-hardware boot
is claimed. DMAC interrupts are covered by the host mask/priority tests,
but have not been exercised by an attached DMA device in this configuration.

## Embedded FFS root and static n32 init: userspace reached

Build with:

```sh
JOBS=16 sh build-ps2-root.sh
```

The script builds tools and the `ELFROOT` kernel, assembles and statically
links `tests/ps2/init-n32.S`, creates a 2 MiB little-endian FFSv1 image using
`nbmakefs`, and embeds it using `mipsel--netbsd-mdsetimage`. The result is
`build/ps2/netbsd-ELFROOT.elf`. Intermediate files are under
`build/ps2/root/`. The root contains `/sbin/init` and `/dev/console`; no host
root privileges or real device nodes are needed because makefs reads an
mtree specification for the console node (character major 2, minor 0).

The init ELF has flags `abi2, 5900, mips3` (n32), entry `0x00400000`, a
NetBSD identification note, and no interpreter or shared-library dependency.
Disassembly confirms scalar instructions only: no MMI or FPU instructions.
It opens `/dev/console`, checks a 64-bit shift/add/shift result, writes a
success marker, checks the returned byte count, and calls `reboot(RB_HALT)`.
On failure it loops for register inspection instead of exiting PID 1.

The SBIOS-free SIO console now has a minimal output character device,
`eesio` (PS2 major 9), behind the standard console node. It uses uiomove
and polled UART transmission. This is not a tty driver: user read, terminal
ioctls, and a shell are not implemented by this device.

The first root-image run (12) mounted FFS, then crashed before exec during
TOD initialization. `cpu_initclocks()` had registered the address of a
stack-local `todr_chip_handle`, leaving a dangling pointer. The handle is
now static for the SBIOS path. The no-SBIOS path does not register a TOD
clock because the BIOS RTC bootinfo is unavailable, so NetBSD uses the
filesystem time and prints the corresponding warnings.

Run 13, with Full TLB, EE cache enabled, and EE/IOP interpreters, reached
the following at the absolute event boundary 24,000,000:

```text
[   1.0199392] root on md0a dumps on md0b
[   1.0199392] root file system type: ffs
[   1.0199392] kern.module.path=/stand/mips-n32/11.99.9/modules
[   1.0199392] WARNING: no TOD clock present
[   1.0199392] WARNING: using filesystem time
[   1.0277133] WARNING: CHECK AND RESET THE DATE!
PS2 n32 init: userspace write and 64-bit arithmetic OK
[   1.0277133] syncing disks... done
[   1.0277133] unmounting file systems...
[   1.0299387] unmounting done
[   1.0299387] SBIOS unavailable; CPU halted.
```

The kernel ELF SHA-256 is
`940bdbc0ecf4fd0db37a00f7d488d6397b6018333a830267b93d185ed913bfc3`.
The init SHA-256 is
`21083563717be197ec4fd2c61bf80a1cc0cf15d88b2cd3806c1a8ada78af6a7f`.
No observation fault occurred, all 2859 SIO bytes were saved, and the
emulator was stopped normally after the guest halted. The two cache/SPL
host checks and `git diff --check` pass; the kernel has no undefined symbols.

This verifies root mounting, exec of a static n32 PID 1, userspace scalar
64-bit arithmetic, console open/write syscalls, and the reboot syscall's
halt path. It does not yet provide libc, a normal init, a shell, multi-process
stress, signal validation, or real-hardware validation. Full 128-bit GPR
and EE-specific context preservation remains a separate next step; this
init deliberately avoids MMI and does not test the upper 64 bits.

## R5900 integer context preservation implemented

The R5900 trapframe now has a 16-byte-aligned full GPR snapshot plus HI1,
LO1, and the opaque MFSA value. Its size is 864 bytes (previously 320).
The existing n32 scalar slots remain authoritative for the low 64 bits,
HI0/LO0, syscall results, signal arguments, PC, and Status. Static assertions
enforce the alignment required by SQ/LQ.

`mips/ee_context.h` captures GPRs with SQ before normal exception code
clobbers them. On return, it merges each saved upper half with the live
scalar lower half using PCPYLD, and restores HI1/LO1 and SA. SA is copied
without interpreting its encoding; the required three-instruction separation
before MTSA is respected. Together with the existing MFHI/MFLO and MTHI/MTLO
paths, this protects the full 128 bits of both HI and LO.

Capture/restore covers user exceptions, hardware interrupts, syscalls, and
kernel exceptions/interrupts. NMI capture also includes the added state;
the existing NMI handler does not resume. The R5900 reserved-instruction
entry takes the full exception path rather than the generic RDHWR shortcut.
Fast TLB refill continues to use only the reserved kernel temporaries.
GPR zero is immutable; k0/k1 are OS-reserved and are not preserved for user
code. All other GPR upper halves, including sp/gp/ra, are protected.

The user state lives in each LWP's trapframe before entering C or the
scheduler. Thus a blocking syscall or context switch does not rely on upper
halves remaining live in hardware, and the existing whole-trapframe copy in
`cpu_lwp_fork()` also copies the EE state. `setregs()` clears it on exec.
The kernel's ordinary function-call/switch ABI remains n32; this change
does not define an ABI for retaining MMI values across ordinary C calls.

PS2 n32 `mcontext_t` now appends GPR upper halves, HI1, LO1, and SA. The
new `_UC_EE` flag makes `cpu_getmcontext()`/`cpu_setmcontext()` transfer
these fields, including during signal delivery and setcontext return.
The n32 calling convention and sizes of pointers/longs remain unchanged,
but **the PS2 n32 signal/context layout is extended**: ucontext_t is now
952 bytes instead of 664. Future libc/userland must be built against these
PS2 headers; generic prebuilt n32 signal/context libraries cannot be assumed
compatible. Other platforms' context layouts are unchanged. O32 signal
compatibility is not validated by this work.

EE userspace Status now enables EIE, which generic MIPS PSL_USERSET lacked.
R5900 exec does not request the generic UX/KX/FR modes, and the initial
kernel LWP and exception-return macros no longer require KX on R5900.

The integration init is built with:

```sh
JOBS=16 INIT_SOURCE=tests/ps2/init-ee-context.S sh build-ps2-root.sh
```

`tests/ps2/ee-context-layout.c` checks its ucontext, siginfo, and sigaction
offset assumptions against the real cross-compiled PS2 headers. The init
seeds a distinct upper-half pattern in every non-reserved GPR and arbitrary
64-bit patterns in HI0/LO0/HI1/LO1, then checks them and the saved raw SA
value after:

- a syscall;
- five million user loop iterations with interrupts enabled;
- a 50 ms nanosleep (blocking/scheduling and timer wakeup);
- BREAK/SIGTRAP and SIGUSR1 delivery/return, with the handler deliberately
  clearing every tested upper half, both halves of HI/LO, and SA;
- fork, and parent wait while the child clears its own state and sleeps.

Final Full-TLB, EE-cache-enabled interpreter run 16 sampled PC `0x00400070`
inside the user loop at event 20,000,000. Status was `0x00011c11`
(KSU=user, EXL=0, IE=1, EIE=1), and GPR snapshots contained the seeded
upper-half patterns. At event 80,000,000 it had printed:

```text
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
[   1.2199377] syncing disks... done
[   1.2199377] unmounting file systems...
[   1.2199377] unmounting done
[   1.2199377] SBIOS unavailable; CPU halted.
```

All 2884 SIO bytes were saved, observation remained ready without faults,
and the emulator was stopped normally. The cache/SPL host tests,
cross-compiled ABI layout assertions, kernel link, and `git diff --check`
pass. The final test kernel ELF SHA-256 is
`01feb50760a5f3d15c36492f73b2f39a19c06c2788cd14a65d4ea9ff29a32c0e`.
The integration init SHA-256 is
`bc0e2762e4cb819deb48a6e7c74e48071c5de639bbb7d1feffb88672325f98b7`.

This implements integer-context protection. VU0/VU1 context switching and
hardware FPU enablement are not included; the kernel continues to use
NOFPU/soft-float. Existing ptrace/general-register core-note interfaces
still expose scalar registers and do not yet expose the EE extension.
Alternate signal stacks, nested-signal stress, O32 compatibility, kernel
MMI use across C calls, and real PS2 hardware remain unvalidated.

## Static n32 libc and C init: all checks passed

The next userspace milestone builds NetBSD's own csu, libgcc, and libc for
n32/R5900, then installs `tests/ps2/init-libc.c` as the RAM disk's PID 1.
This is a static C program using the normal libc startup, TLS, allocator,
stdio, signal trampoline, and syscall wrappers, rather than hand-written
syscalls alone.

Reproduce with:

```sh
JOBS=16 sh build-ps2-user.sh all
python3 tests/ps2/userland_elf_test.py
```

Individual stages are `toolchain`, `headers`, `libs`, and `init`. The
toolchain stage rebuilds/installs the existing cross GCC so an already built
checkout also picks up the TLS backend change. It bootstraps the original
PS2 tool build if needed. Objects, headers, static libraries, compiler
wrapper, init binary, and logs are isolated under `build/ps2/user-n32/`.
The final kernel containing the RAM disk remains
`build/ps2/netbsd-ELFROOT.elf`.

The experimental userspace wrapper selects `MACHINE=playstation2` and
`MACHINE_ARCH=mips64el` (NetBSD's n32 build conventions), while reusing
the existing `mipsel--netbsd` cross tools. It explicitly requests
`-march=r5900 -mabi=n32 -msoft-float -mno-llsc` and n32 linker emulation
`elf32ltsmipn32`, including for intermediate relocatable links. A compiler
wrapper carries these options into dependency generation as well as normal
compiles; mkdep does not preserve options embedded in the CC variable.
The static smoke-test profile disables PIC library variants, PIE, C++,
compat libraries, profiling, manuals, SSP, and fortify build defaults.
It does not change the port's existing default o32 distribution target.

Two additional portability problems were found and fixed:

1. Generic MIPS GCC emits `rdhwr $3,$29` for TLS, relying on reserved-
   instruction emulation on older CPUs. On EE the same word `0x7c03e83b`
   is an SQ instruction, so the initial libc run 17 faulted at
   `je_malloc_tsd_boot0` while attempting that store. The NetBSD/R5900
   GCC backend now obtains the thread pointer with a normal call to the
   existing `_lwp_getprivate` wrapper. The compiler models the call's
   clobbers and saves live arguments normally. No custom trap encoding or
   PCSX2 change is involved. Other MIPS architectures retain RDHWR.
2. NetBSD's n32 libc selected LL/SC atomics. PS2 now uses UP-only RAS
   CAS32/CAS64 sequences, with other operations built around CAS. libc
   registers both ranges at startup and exits if registration fails;
   there is no fallback to an LL/SC spinlock. R5900 simple locks likewise
   use the platform CAS backend. The installed GCC's configured LL/SC
   default also overrode the ISA's normal R5900 exclusion, leaving LL/SC
   in an initially unexercised `realloc(ptr, 0)` path. Both PS2 kernel and
   userspace build flags now explicitly disable LL/SC. Libraries and the
   test kernel were rebuilt after changing these flags.

Run 18 reached the C program and passed its initial tests. Final run 19
also tests realloc-to-zero and C11 atomics after removing the remaining
LL/SC instructions. With Full TLB, EE cache enabled, and interpreters, it
produced the following by absolute event 80,000,000:

```text
PS2 n32 libc: main pid=1 ptr=4 long=4 context=952
PS2 n32 libc: TLS OK
PS2 n32 libc: malloc/string OK
PS2 n32 libc: integer/soft-float/printf OK
PS2 n32 libc: RAS atomic32/64 OK
PS2 n32 libc: C11 atomic32/64 OK
PS2 n32 libc: signal OK
PS2 n32 libc: fork/wait OK
PS2 n32 libc: ALL CHECKS PASSED
[   1.1899395] syncing disks... done
[   1.1899395] unmounting file systems...
[   1.1899395] unmounting done
[   1.1899395] SBIOS unavailable; CPU halted.
```

The test checks initialized compiler TLS and fork isolation, heap/string
operations, 64-bit division/remainder, double arithmetic and formatting
through software floating point, CAS success/failure and 10,000 increments
at each width, C11 atomic operations, libc signal delivery/return, and a
child's exit status. All 3106 SIO bytes were saved, no observation fault
occurred, and the emulator was stopped normally after the guest halted.

`userland_elf_test.py` checks both R5900 and generic MIPS TLS code generation
and scans the final static init's 166815 disassembled instructions for
LL/SC-family encodings and the RDHWR/SQ collision. It passes, as do the
existing cache/SPL host tests, shell syntax checks, and `git diff --check`.
The init has no undefined symbols and no dynamic interpreter. It is about
918 KiB and fits in the existing 2 MiB root image.

Final C-init kernel ELF SHA-256:
`37a5e66c32abda7df000debd90ab9d632e6759dc18156c4912fdca04d0962a5c`.
Static init SHA-256:
`a07bea8d557ef6a9c2a367032e399bd75307db9afc3397497a8bce25f541f963`.

This is a functional libc-based n32 userspace smoke test, not yet a full
NetBSD distribution or interactive shell. The SIO device remains output-only
for user processes. Shared-memory atomic stress, pthreads, dynamic linking,
complete software floating-point conformance, VU/FPU state, and real hardware
are not validated. The next practical step is console input/tty support and
a small n32 shell/root tree built against this libc and the PS2 context ABI.

## GENERIC-derived PS2 hardware configuration

`sys/arch/playstation2/conf/PS2` now includes the existing `GENERIC` and
applies small-memory overrides. `GENERIC` itself is unchanged. Inspection
showed that its enabled physical drivers already have PS2 attachment paths;
there were no enabled PCI/ISA or other unreachable physical-device families
to remove. The reductions below are optional functionality, not a claim
that these features cannot work on PS2.

Retained physical paths are GS/wsdisplay, SBUS/DEV9 SPD with ATA HDD and
SMAP Ethernet, OHCI USB with hubs, HID keyboard/mouse, and ugen. IPv4/IPv6,
FFS with WAPBL, NFS/DHCP root, PTYFS/ptys, packet capture, clockctl, wsmux,
and ksyms remain available.

The derived profile sets maxusers to 8 rather than 16 and removes MFS,
NULLFS, UNION, KERNFS, PROCFS, NPF, vnd, gif, stf, swwdog, and verbose
sysctl description strings. The embedded kernel configuration is retained.
An optional `PS2.local` is included after these overrides. An existing
`GENERIC.local`, if present, would be inherited before them; neither local
file was present in this verification.

Build:

```sh
JOBS=16 KERNEL=PS2 sh build-ps2.sh
```

The result is `build/ps2/netbsd-PS2.elf`. Both PS2 and the original GENERIC
built and linked successfully using the current checkout and same tools.
Both have no undefined symbols. Generated attachment tables and symbols
confirm that the physical devices listed above remain in PS2. Configured
maxusers is 8, IPv4/IPv6 are present, and gif/stf counts are zero.
`git diff --check` passes.

| Measurement (bytes) | GENERIC | PS2 | Reduction |
| --- | ---: | ---: | ---: |
| text | 2,474,020 | 2,244,488 | 229,532 |
| data | 112,400 | 110,992 | 1,408 |
| BSS | 79,984 | 79,312 | 672 |
| text + data + BSS | 2,666,404 | 2,434,792 | 231,612 |
| ELF file | 3,425,108 | 3,123,224 | 301,884 |

The section sum is about 226 KiB smaller (8.7%); the file is about 295 KiB
smaller (8.8%). These are static build measurements, not measured available
memory after boot or a performance benchmark. Runtime allocations, page
rounding, and symbols also affect memory use.

This profile preserves GENERIC's SBIOS/bootinfo loader contract and linked
entry `0x80010000`; it is not the BIOS-loaded SBIOS-free direct ELF profile.
Do not substitute it for the previously tested `ELFROOT` image in the
PCSX2 direct-boot procedure. No runtime boot or physical-device operation
is claimed for the new PS2 profile. Its ELF flags identify n32/ABI2 and
R5900, and its SHA-256 is
`048ca766588d0ec5419f3af676e23cf411fe0c0c21f752d556c18af387c4ffe3`.
Build logs are saved as `build/ps2/logs/PS2-kernel.log` and
`build/ps2/logs/GENERIC-kernel.log`.

## GENERIC-derived SBIOS-free PS2ELF: runtime verified

`PS2ELF` includes `PS2`, so its configuration chain is now
`GENERIC -> PS2 -> PS2ELF`. It applies the known direct-entry addresses
(linked text `0x80100000`, ELF entry `0x00100000`), selects PS2_NOSBIOS,
and adds the existing 2 MiB embedded FFS root.

It explicitly removes the current GS/wscons and SBUS/DEV9/USB attachment
trees and their console options. Those are physically possible PS2 devices;
they are omitted here because their current bootinfo/SBIOS/IOP driver path
is unavailable in this direct-boot setup. The original PS2 hardware profile
continues to retain them. NFS/network root is removed because no external
NIC is attached. IPv4/IPv6 and loopback, PTYFS/ptys, FFS/WAPBL, and the
remaining small PS2 profile facilities are retained for future userspace.
Their inclusion is not a runtime device/network validation.

`build-ps2-root.sh` now accepts KERNEL, defaulting to ELFROOT as before.
With the previously built static libc init:

```sh
JOBS=16 KERNEL=PS2ELF \
    INIT_BINARY="$PWD/build/ps2/user-n32/init-libc" \
    sh build-ps2-root.sh
```

Alternatively, `JOBS=16 KERNEL=PS2ELF sh build-ps2-user.sh all` builds the
userspace prerequisites as well. The primary result is
`build/ps2/netbsd-PS2ELF.elf`. Existing ELFROOT and SBIOS PS2 images remain
separate files. An optional PS2ELF.local is read after the profile overrides.

PCSX2 run `run20-ps2elf` used Full TLB, EE cache, interpreters, and the same
deterministic agent profile as the preceding tests. By event 80,000,000:

```text
[   1.0000000] total memory = 32768 KB
[   1.0000000] avail memory = 27224 KB
[   1.0299415] root on md0a dumps on md0b
[   1.0299415] root file system type: ffs
PS2 n32 libc: main pid=1 ptr=4 long=4 context=952
PS2 n32 libc: TLS OK
PS2 n32 libc: malloc/string OK
PS2 n32 libc: integer/soft-float/printf OK
PS2 n32 libc: RAS atomic32/64 OK
PS2 n32 libc: C11 atomic32/64 OK
PS2 n32 libc: signal OK
PS2 n32 libc: fork/wait OK
PS2 n32 libc: ALL CHECKS PASSED
[   1.1999402] SBIOS unavailable; CPU halted.
```

The guest completed syncing and unmounting before that halt. All 3104 SIO
bytes were saved and observation remained ready without a fault. The VM
was then stopped normally.

A second image uses the same unpatched PS2ELF kernel with the MMI context
init in a separate `build/ps2/root-ee/` RAM disk. It is saved as
`build/ps2/netbsd-PS2ELF-ee.elf`; the primary C-init image is unchanged.
Run `run21-ps2elf-ee` reached the same event boundary and printed:

```text
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
[   1.2299370] SBIOS unavailable; CPU halted.
```

All 2882 SIO bytes were saved, with no observation fault, and this VM was
also stopped normally. This verifies that integer-context preservation
continues to work with the GENERIC-derived profile.

The kernel links without undefined symbols and contains none of the
checked SBIOS, GS framebuffer, OHCI, or SMAP attachment symbols. The
cross-compiled context-layout assertion, three cache/SPL/userspace host
checks, shell syntax, and `git diff --check` pass.

With the RAM disk included, text/data/BSS sum to 4,089,920 bytes, versus
3,650,772 bytes for the minimal ELFROOT image: the broader profile costs
about 429 KiB in these sections. Its reported boot-time available memory
is 440 KiB below the preceding minimal ELFROOT run. These are different
feature sets; the extra size is not an unsuccessful driver reduction.

C-init PS2ELF SHA-256:
`9ad5d4bb5f0c92013822824e61c021f265efaec4c448246a59322bea55e28235`.
MMI-test PS2ELF SHA-256:
`86f9d1ded4105076658ad2c5fafb006e09ffcbc59e67e6118b53611bc0df7455`.

PS2 remains the SBIOS hardware profile with build-only validation. PS2ELF
now has the direct-boot/runtime validation above. Neither run validates
physical IOP peripherals, an interactive shell/tty, VU/FPU context, or
real hardware. No PCSX2 source/settings changes were required.

## SBIOS/RTE-free interactive RAM root (2026-10-16)

The next development profile is `PS2SHELL`, derived from PS2ELF, with a
12 MiB embedded FFS root. Build it with:

```sh
# Once, or after compiler/ABI changes:
sh build-ps2-user.sh toolchain
sh build-ps2-user.sh headers
sh build-ps2-user.sh libs
# Build userspace, filesystem, kernel, and embed the filesystem:
sh build-ps2-shell.sh
```

Result: `build/ps2/netbsd-PS2SHELL.elf`. The static n32 root contains NetBSD
sh (SMALL, without libedit/history), cat, ls, mkdir, rm, uname, and shell
builtins. A development PID 1 remounts `/` read/write, establishes a
controlling console session for sh, reaps children and restarts an exited
shell. Reopening console descriptors after session exit handles tty vnode
revocation. Minimal halt/reboot/sync tools are included. There is no login
authentication or full rc/service environment; this is a development root.
Passwd/group text is present, but a password database is not built; numeric
owner display (`ls -ln`) is the tested path.

The direct EE SIO character device now uses the normal tty line discipline:
canonical reads, echo, tty ioctls, foreground process groups and signals,
poll/kqueue, with receive polling once per kernel tick. Each receive pass
is bounded to 256 bytes. Kernel console polling suspends the callout.
TTY output uses its own newline processing; early kernel output retains
CR/LF conversion. Serial baud initialization is still supplied by the boot
environment/emulator; physical SIO setup has not been validated on hardware.

`run22-shell` first reached the prompt, but exposed the read-only root.
`run23-shell-rw` verified remounting, file creation/read/removal, directory
creation and listing, arithmetic (123*7 = 861), a pipeline, background
execution/wait, `/dev/null`, `/dev/tty`, and Ctrl-C terminating a foreground
cat and returning to sh. The observed `FILES_OK`, `FORK_OK` and `SIGNAL_OK`
markers were assembled by printf and were absent from the submitted input.
The shell session-exit path motivated reopening the console in PID 1;
the final run below verifies restart after sufficient guest time advances.

Final `run24-shell-final`: Full TLB, EE cache enabled, EE/IOP interpreters,
32 MiB RAM, the isolated deterministic agent profile. By event 80,000,000:

```text
total memory = 32768 KB
avail memory = 17088 KB
root on md0a dumps on md0b
root file system type: ffs
PS2 RAM root: n32 shell, direct EE SIO, no SBIOS/RTE
ps2#
```

Commands created `/tmp/probe`, read it through a pipeline, listed its
numeric ownership and size, printed `ROOT_OK`, and exited. PID 1 printed
`init: shell ended; restarting`; the new prompt was observed by event
240,000,000. A second input read back `persist`, printed `RESTART_OK`, and
ran halt. At event 260,000,000 the log contained:

```text
RESTART_OK
[   4.0499420] syncing disks... done
[   4.0499420] unmounting file systems...
[   4.0599421] unmounting done
[   4.0599421] SBIOS unavailable; CPU halted.
```

All 3135 EE SIO bytes were saved, all 143 submitted bytes were consumed,
observation remained ready with no fault, and the PCSX2 process was stopped.
These guest outputs establish the tested results; the transport's generic
`guest_console_verified=false` and `outcome=unknown` do not infer them.
The build script syntax, diff whitespace checks, TLS codegen check, and
instruction scans of all eight distinct root executables passed: no LL/SC
family opcode or the previously identified RDHWR/SQ collision word.

Final ELF SHA-256:
`03cbbb653eee4e17cecddbe2d07519025b42972e724e7392b593e8ed822e850e`.

RAM-root changes disappear on reboot. Twelve MiB currently reserves space
for separately linked executables and temporary files; a future crunched
image can reduce the duplicated libc footprint. The filesystem has only
64 inodes, appropriate for this small test root, not a general installation.
USB, HDD, Ethernet, hardware SIO and physical PS2 operation remain untested.

The intended peripheral architecture is native NetBSD support for IOP boot,
SIF transport/RPC, and the required IOP modules/services, using ps2dev/PS2SDK
documentation and implementations as references. It must not require SBIOS
or the Linux RTE. No such new IOP driver is implemented in this milestone.

There is currently **no GS screen output**: PS2SHELL inherits PS2ELF's
disabled SBIOS-dependent gsfb/wsdisplay. The interactive console is EE SIO,
not a rendered terminal. The deterministic agent profile also pins Null GS;
selecting software rendering cannot make this kernel draw pixels. Per the
user's preference, future graphical-console validation should use the
software renderer in an ordinary isolated run (or a separately extended
deterministic profile). The user's existing rendering settings were not
changed for these console-only tests.

## Direct GS text output and development console (2026-10-16)

`PS2GS` includes PS2SHELL and enables the new `PS2_GSCONSOLE` option.
Build with `KERNEL=PS2GS sh build-ps2-shell.sh`; the result is
`build/ps2/netbsd-PS2GS.elf`. The original SIO-only profiles are retained.

`ee/gs_console.c` programs PCRTC directly, using the existing port's VGA
timings and the supplied EE User Manual ch.7 / GS User Manual ch.4/5/7
transfer definitions. It sends aligned 128-bit LQ/SQ packets to the GIF
PATH3 FIFO, using PACKED A+D setup and IMAGE payloads. No DMA, VU, SBIOS,
RTE or IOP RPC is used by this implementation. A bounded FIFO-full wait
disables the GS mirror on timeout, allowing SIO output to continue.

The GS receives the same characters as SIO output, including tty echo and
userspace writes. Sony's existing BSD-licensed 8x16 font is rendered in
PSMCT32, with a pale-gray foreground and dark-blue background. CR, LF,
backspace, tab and form-feed screen clearing are supported; scrolling uses
GS local-to-local copying. This is a simple console mirror, not a wsdisplay
driver or an ANSI/VT terminal. Input remains the EE SIO tty and TERM=dumb.
No Japanese font or USB keyboard support is added.

The first `run25-gs-sw` failed before kernel diagnostics: GS initialization
called splhigh before mach_init initialized mips_splsw. Its snapshot showed
execution in the previous BIOS exception path with a zero-address fault.
Replacing that call with direct `_intr_suspend` / `_intr_resume` lets the
early console serialize packets without depending on SPL initialization.
`run26-gs-sw` then reached the shell and displayed both boot diagnostics and
command results. The initial NTSC 640x240 display had tall characters and a
clipped last line. At the user's request, the final implementation uses
**VESA VGA 640x480, noninterlaced, 80 columns x 30 rows**. PLL stabilization
uses a 2.5ms CP0 Count wait, because delay() is not initialized this early.

`run27-gs-vga` used the specified PCSX2 executable, software renderer
(Renderer=13), Full TLB, EE cache, interpreters, and 32 MiB memory in a new
`data-sw` profile. Thirty-five numbered output lines scrolled correctly,
followed by a file read, `VGA_OK`, and the shell prompt. The final
`run28-gs-vga-final` also verified form-feed clearing, uname, RAM file I/O,
and visible `VGA_OK` / `ps2#`. The response marker was constructed from
separate printf arguments, not matched against command echo. All 194 input
bytes were consumed and all 3028 output bytes saved without observation
fault. The original SIO-only boot reports remain in the earlier sections.
Available memory is 17080 KiB. The final kernel links without undefined
symbols; shell syntax and diff whitespace checks pass.

Final PS2GS ELF SHA-256:
`c0b70533078ae363518b91490c8168d4de9c7a5b139c1c405386b46ff0020ada`.
The 640x448 intermediate build was not run; VGA superseded it at the user's
request. Prior agent runs through run29 have all been stopped.

For development, the isolated `data-sw/PCSX2/inis/PCSX2.ini` now enables
Logging/EnableVerbose, EnableFileLogging, EnableEEConsole, EnableIOPConsole,
EnableLogWindow and ShowEESIOInput; AttachLogWindowToMainWindow is false.
Software rendering remains selected. PCSX2's `-agent-console` option forces
nogui/batch and blocks the GUI log sink in this fork, so automatic CLI tests
use RAW capture and file logging. The current developer session instead
uses ordinary GUI startup with the same isolated data directory and ELF:

```text
pcsx2-qtx64-avx2-clang.exe -datapath <probe-root>/data-sw
    -elf <probe-root>/netbsd-PS2GS.elf -unlimited
```

Both the GS window and development log window were observed. GUI SIO input
controls are visible, but command submission from that field has not been
verified; CLI SIO command submission was verified above. The ordinary GUI
session was left running for the user. This is not a deterministic profile:
the standard configured peripherals and host clock are not frozen.

Verbose output exposes repeated `Cache mode b not implemented`. NetBSD's
R5900 definition maps 0x0b to instruction-cache HIT INVALIDATE. The inspected
PCSX2 Cache.cpp has an explicit no-op for 0x07 (instruction index invalidate,
no instruction cache modeled), but 0x0b reaches the generic warning. This
explains the warning category; no emulator source change was made, and it
does not by itself establish incorrect execution or a data-cache failure.

The font size/colors are software choices, not a GS hardware restriction.
The current choice is 8x16 at 640x480; an 8x8 font would yield 80x60 at the
same resolution. No font-size or palette change was requested/implemented
after the user's question comparing the appearance with Linux.

Physical VGA output, analog encoder/DVE setup, and real hardware operation
are not validated. In particular, this code does not replace IOP-side DVE
programming; successful PCSX2 scanout is not proof of complete physical
video initialization. The console is still booted by PCSX2's ordinary BIOS
ELF loader; “SBIOS-free” means the running NetBSD driver makes no SBIOS/RTE
calls, not that the emulator has no BIOS boot stage.

At the user's subsequent instruction, Computer Use is no longer to be used;
further work must use CLI/configuration/log-based operations. Earlier visual
checks preceded that instruction. No PCSX2 source files were changed.

## EE recompiler with expanded memory: regression results (2026-10-16)

The user enabled EE recompilation and the 128 mode. Reading the actual
development INI confirmed EnableEE=true, EnableIOP=false, ExtraMemory=true,
EnableExperimentalEETLB=true, EnableEECache=true and Renderer=13. In this
fork ExtraMemory expands emulated EE RAM to 128 MiB and IOP RAM to 8 MiB;
it is unrelated to the width of GPRs. NetBSD still uses the 32 MiB constant
in bootinfo.h. This test does not validate allocation/access above 32 MiB.

The settings were copied to `data-jit128` for the tests, leaving the user's
development profile unchanged. Both agent runs reported ee_recompiler=true,
extra_memory=true and full_tlb=true in effective-config.json. They used
ordinary, nondeterministic agent execution, not the Null-GS deterministic
profile. All interaction was through CLI/files/logs; no Computer Use.

`tests/ps2/build-jit-regress.sh` builds supplemental static n32 tests and
embeds a separate root in `netbsd-PS2GS-jit128.elf`. It requires the existing
PS2GS kernel and shell/libc prerequisites and does not replace the normal
PS2GS ELF. It also creates `netbsd-PS2GS-libc.elf`, using the existing libc
PID-1 smoke-test image with the same raw PS2GS kernel.

`run30-jit128-libc` passed TLS, malloc/string/realloc, integer/soft-float,
RAS and C11 atomics, signal delivery, and fork/wait. It printed ALL CHECKS
PASSED, synced/unmounted and halted. All 3102 SIO bytes were saved without
observation fault, and the VM was stopped.

`run31-jit128-shell-ee` started the shell and executed the new jit-regress
program. It tests 64 successive instruction replacements at the same
already-executed virtual address, with `_cacheflush(..., BCACHE)` between
store and call. A child then rewrites the same VA after fork; its code must
return 123 while the parent's original code still returns 64. Finally,
loads from a PROT_NONE mapping test SIGSEGV delivery, fault address, and
signal ucontext's EPC/BD, both directly and in a taken branch's delay slot.
The handler resumes past the faulting sequence and the program exits zero.

```text
JIT regression: code rewrite/cacheflush x64 OK
JIT regression: fork/COW code isolation OK
JIT regression: load exception EPC/BD=0 OK (cause=00000408)
JIT regression: delay-slot exception EPC/BD=1 OK (cause=80000408)
JIT regression: ALL CHECKS PASSED
jit-status=0
```

RAM-file creation/read/removal, ls, and a pipe also succeeded, producing
SHELL_OK. The shell then executed `exec /bin/ee-context`. This uses the
existing raw n32 test, with distinct upper-GPR patterns and full integer
HI/LO, HI1/LO1 and SA state, across syscalls, a long user loop allowing timer
interrupts, sleep, synchronous BREAK, an asynchronously delivered signal,
and fork/wait with child state clobbering. The result was:

```text
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
[  34.0899506] syncing disks... done
[  34.0899506] unmounting file systems...
[  34.0899506] unmounting done
[  34.0899506] SBIOS unavailable; CPU halted.
```

All 161 input bytes were consumed and all 3445 SIO bytes saved, without
observation fault. The VM was stopped. The supplemental executable passed
the R5900 opcode scan (no LL/SC family or known RDHWR/SQ collision word),
and script syntax/diff whitespace checks passed.

These tests ran with the GS mirror and GIF LQ/SQ transfer path enabled;
they validate successful CPU execution through that path, not a fresh
visual check of pixel contents. The exception checks specifically exercise
protected-page load faults and their branch-delay behavior; they are not
exhaustive coverage of every TLB/ASID/privilege/exception combination.
Likewise, the code-update/COW tests cover useful JIT invalidation cases,
not all mappings or all CACHE opcodes. The kernel was unchanged during this
regression run. The results support EE recompilation as the normal
development setting, retaining interpreter runs for comparisons.

JIT regression ELF SHA-256:
`b7306a557317d64891d21a3086525d7889e05a9c03f99a754980eee0fabece9f`.
Libc regression ELF SHA-256:
`c6899f856d49946348f982246cfb081da342a8082810a57085bb6facdf1a0358`.

## NetBSD manages 128 MiB of EE RAM (2026-10-16)

The direct ELF development configurations ELFBOOT and PS2ELF now explicitly
set `PS2_MEMSIZE=128`, inherited by ELFROOT/PS2SHELL/PS2GS. machdep uses
this option for physmem, heap initialization and UVM physical-page loading.
Only 32 and 128 are accepted, and expanded memory requires PS2_NOSBIOS.
SBIOS GENERIC/PS2 configurations retain the 32 MiB default. This is a
fixed development target, not automatic hardware RAM detection.

The legacy bootinfo ABI in bootinfo.h stays at 0x01fff000; changing the
kernel's usable RAM must not move the address expected by SBIOS loaders.
Direct ELF profiles do not use that bootinfo. The original reservation of
the last 4 KiB remains at the configured RAM end; mips_init_msgbuf then
reserves its buffer from the high end of UVM RAM. Kernel/low-loader areas
and VM metadata remain unavailable to user allocations as usual. The
physical cluster count is now explicitly initialized to 2, matching the
existing kernel/heap entries used by physical-memory access and dump code.
A kernel-size check prevents underflow in the free-range calculation.

128 MiB fits below the R5900 KSEG0/KSEG1 direct-map limit and below EE MMIO
at 0x10000000. The port's bus_dmamem allocator derives its range from
pmap_limits rather than a 32 MiB constant. No IOP/device DMA testing was
performed: device-specific addressing limits and bounce buffering still
need consideration when native peripheral drivers are implemented.

`run32-ram128` used EE JIT, Full TLB, EE cache, expanded RAM, IOP interpreter,
and software GS rendering. It booted the updated PS2GS kernel:

```text
load memory 0xeea000, 0x7115000
total memory = 128 MB
avail memory = 111 MB
```

The heap range ends at 0x07fff000 before msgbuf/bootstrap reservations.
`tests/ps2/mem128.c` checks hw.physmem64, allocates 80 MiB anonymously,
writes a unique pattern to every 32-bit word, confirms every page resident
with mincore, and requires zero configured swap devices. It then verifies
all words, replaces them with inverse patterns, verifies in reverse order,
and unmaps the allocation. This exceeds the entire former 32 MiB capacity
while preventing a successful result through aliased memory or swap.
It avoids mlock's intentional global wired-page limit (one third of RAM)
instead of changing that kernel policy for the test.

```text
RAM128: physical=128 MiB
RAM128: writing 80 MiB
RAM128: resident=10240 pages (80 MiB), swap=0, maxrss=82520 KiB
RAM128: resident 80 MiB forward/inverse/reverse OK
RAM128: ALL CHECKS PASSED
mem-status=0
```

The same shell then passed all jit-regress tests (64 code rewrites, fork/COW
code isolation, normal and delay-slot protected-page faults with precise
EPC/BD), followed by exec of the EE-context test:

```text
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
```

The guest synced, unmounted and halted at 67.1899529; all 91 input bytes
were consumed and all 3510 EE SIO bytes saved, without observation fault.
The test VM was stopped. `run33-ram128-libc` independently passed every
libc test on the updated kernel, synced/unmounted/halted, and was stopped.
Its 3098 SIO bytes were saved without observation fault.

Cache/SPL host checks, the new executable's R5900 ISA scan, script syntax,
undefined-symbol check and git diff whitespace check passed. No Computer
Use or PCSX2 source changes were involved. Runtime checks cover 80 MiB of
simultaneous anonymous residency, not a destructive sweep of every RAM byte.

The normal `build/ps2/netbsd-PS2GS.elf` and its Windows probe-root copy now
contain the 128 MiB kernel with the normal shell root. Supplementary images
include mem128/jit-regress/ee-context in a separate test root; build these
after the normal build with `sh tests/ps2/build-jit-regress.sh`.

Normal PS2GS SHA-256:
`4a89dc1cbfd18e5385324a4a29cc91c5c76eaaaea66a4c7cfed0f3002c719434`.
128 MiB memory/JIT/EE test image SHA-256:
`bf707c63810376449969806485e6aa5d2e69392db6e54f1128cfa4f7d78e7d09`.
128 MiB libc test image SHA-256:
`4792579ca96e0cc482d627a072cb3f2ec5302d75f4bb1239d70d0bfdef47fba4`.

## Native SIF bootstrap without SDK code (2026-10-16)

The user requested continuing toward native IOP/USB support, with an
explicit constraint against directly porting PS2SDK because of its license.
The new `ee/sif_native.c` is a separate implementation marked BSD-2-Clause;
it neither includes nor links SDK headers, libraries, IRX binaries or SBIOS.
No SDK/TGE implementation was imported into the tree. Hardware manuals and
locally installed implementations were read to understand register behavior
and wire formats; this is not a claim of a clean-room process or a legal
opinion about cross-project license compatibility. Future IOP/USB work must
respect the same no-direct-port constraint.

`PS2IOP` derives from PS2GS (128 MiB, native VGA/SIO RAM root) and adds the
`ps2sif0` mainbus device. It is separate from the established PS2GS image:

```sh
KERNEL=PS2IOP sh build-ps2-shell.sh
KERNEL=PS2IOP sh tests/ps2/build-jit-regress.sh  # optional test image
python3 tests/ps2/sif_native_host_test.py
```

The native driver discovers the IOP's SIF command receive buffer through
hardware registers, supplies the EE receive buffer, and sends SIF commands
through direct SIF1 DMA. A short EE END source chain carries an IOP DMA tag
and padded payload; SIF0 destination chains receive the responses. Buffers
own entire 64-byte cache lines and are written/observed through uncached
aliases after initial cache writeback/invalidation. SYNC barriers surround
DMA publication/completion. SIF DMA addresses currently stay in the lower
32 MiB EE range and lower 2 MiB IOP range, even with expanded RAM enabled.

The first RPC operations bind the resident IOP heap service, allocate 4096
bytes, transfer a distinct 64-byte pattern EE->IOP, request its return over
IOP->EE DMA, compare every byte, and free the allocation. Completion checks
include an opaque transaction cookie and the expected RPC operation. Each
wait is bounded using CP0 Count; malformed packets, unaligned/out-of-range
destinations, rounded-DMA overruns and timeouts return errors. Status writes
use only low W1C bits and do not toggle the shared interrupt mask bits.
The native device is rejected at compile time outside a PS2_NOSBIOS kernel
to avoid competing with the existing SBIOS transport.

This is presently a **single-owner, polled bootstrap/self-test**. It has no
public concurrent RPC API, runtime request queue, SIF interrupt service,
IOP reset, IRX loader, IOP interrupt relay, or USB keyboard integration.
The boot ROM's IOP kernel and RPC services remain running. “No SBIOS/RTE”
means NetBSD performs its own EE-side transport, not that an independent
IOP operating system has replaced the ROM services.

`run34-native-sif` initially timed out waiting for RPCINIT, while still
booting the shell. Snapshots showed DMA consumption and a running IOP.
The existing ELF boot loader had already initialized RPC; the one-time
RPCINIT notification is not necessarily repeated. The driver now uses
successful service bind as readiness proof after publishing its receive
address. `run35-native-sif` then passed all bootstrap operations.

Final `run36-native-sif-regress` used the specified PCSX2 executable with
EE JIT, Full TLB, EE cache, 128 MiB EE memory, IOP interpreter and software
GS rendering. It printed:

```text
ps2sif0 at mainbus0: native SIF bootstrap (no SBIOS/RTE)
ps2sif0: CMD/RPC, heap alloc/free, 64-byte DMA round-trip OK (IOP=0x45000 packets=4)
```

After reaching the RAM-root shell it passed the 80 MiB residency/data
test, all jit-regress code-update/COW/EPC/BD tests, and exec of ee-context:

```text
RAM128: resident 80 MiB forward/inverse/reverse OK
RAM128: ALL CHECKS PASSED
mem-status=0
JIT regression: ALL CHECKS PASSED
jit-status=0
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
[  16.0699517] SBIOS unavailable; CPU halted.
```

Sync/unmount completed, all 92 input bytes were consumed, all 3683 SIO
output bytes were saved without observation fault, and the test VM was
stopped. The new host check compiles the production send functions against
a mocked DMA boundary and verifies tag values, every supported word-aligned
length, zero padding, boundary/alignment rejection, timeout behavior and
CMD payload metadata preservation. Link/whitespace/script checks passed;
the kernel symbol table contains no sifbios/sceSif/SifBind/SifCall symbols.
No Computer Use or PCSX2 source changes were used.

Normal PS2IOP ELF SHA-256:
`5de96fc5fe45867c337df8be17e9b30851a00c98ec9a393d34122499cecee367`.
PS2IOP regression ELF SHA-256:
`1dcab08c46914ea0f7fa337130a7f5ad88c4e09e8ceaa77f65fed6597210ec40`.

Next: turn this proof into a serialized runtime IOP-memory/RPC service,
then connect the existing BSD NetBSD OHCI/USB stack to native IOP RAM DMA
and interrupt delivery (or an explicitly polled bring-up path). The old
ohci_sbus glue already uses IOP memory, but its allocation and SBUS setup
currently depend on the SBIOS path and must not simply be re-enabled.
PS2SDK USBD/keyboard IRX modules will not be bundled as a shortcut. A new
IOP-side helper, if needed, must be separately implemented with compatible
licensing and an explicit loading/lifetime/interrupt protocol. None of the
USB, HDD or Ethernet hardware paths has been validated by this milestone.

## Native USB keyboard milestone (2026-10-16)

`PS2USB` extends PS2IOP with the existing BSD NetBSD OHCI/USB/HID stack.
The native SIF driver now serializes sleepable heap RPC calls and reserves
512 KiB of IOP RAM. A native bus_dma tag suballocates that pool and maps
uncached EE IOP-window addresses to IOP physical DMA addresses. It rejects
ordinary EE RAM, out-of-pool ranges, unsupported load types and transfers
that exceed map segment/boundary limits. USB descriptors and payloads stay
in IOP RAM. The bootstrap also verifies the direct RAM window against a
SIF DMA round trip before attaching children.

The OHCI registers are accessed directly at physical 0x1f801600. Completion
handling calls the BSD OHCI interrupt routine once per clock tick; there
is no IOP interrupt forwarding yet. The small ps2kbd boot-protocol driver
feeds the existing console TTY line discipline. It supports US ASCII key
positions, Shift, Caps Lock and Ctrl, suppressing held/duplicate keys and
rollover error reports. JIS-specific punctuation/layout, autorepeat, LEDs,
IME and wscons keyboard integration remain unfinished.

In `run38-native-kbd`, using the specified PCSX2 fork with EE JIT, Full TLB,
EE cache, 128 MiB EE RAM, IOP interpreter and software GS rendering:

```text
ps2sif0: direct RAM window OK, runtime DMA pool 0x45000 + 512 KiB
ohci0 at ps2sif0: native IOP OHCI, tick-polled completions
ohci0: OHCI version 1.0
usb0 at ohci0: USB revision 1.0
uhidev0: KONAMI CPJ1 (0x0510) USB JIS Mini Keyboard (0x0002)
ps2kbd0 at uhidev0: boot keyboard to direct console (US ASCII positions)
ps2# echo usb ok
usb ok
ps2#
```

The user manually typed the command in the PCSX2 rendering window and
confirmed that the command and result appeared. The saved SIO output
corroborates it; serial.ee-sio.rx.bin contains zero bytes, so this command
was not injected through the SIO CLI. No Computer Use was used. This
validates the emulated USB keyboard -> BSD USB/HID -> console TTY -> shell
-> direct GS output path. It is not a retail hardware validation.

No PS2SDK source, headers, libraries or IRX modules are included in this
native path. New native SIF/DMA/OHCI/keyboard files carry BSD-2-Clause SPDX
identifiers; the pre-existing NetBSD USB stack retains its own licenses.
Resident boot ROM IOP kernel/heap services are still required. There is no
IOP reset/module loader, native HDD/Ethernet or persistent root disk yet.

Before committing this milestone, the final PS2USB shell ELF rebuilt
successfully. Cache, SPL, SIF transport and native DMA/HID host tests passed,
as did R5900 TLS/ISA checks for the static shell and init, shell-script
syntax checks and git diff whitespace checks. The build includes the final
duplicate-report and unopened-detach guards; these guards were host-tested
and rebuilt after the manually confirmed run, without a new emulator run.
The earlier run36 memory/JIT/EE-state regression predates USB attachment;
it must not be interpreted as a full USB-enabled runtime regression.

## USB input hardening and physical keyboard mode (2026-10-17)

The normal PS2USB profile now decodes US and JIS ASCII positions, with Shift,
Caps Lock and Ctrl. It selects JIS for HID country code 15 or the Konami CPJ1
0510:0002 device; other unspecified devices retain US. The key positions were
checked against the existing BSD dev/hid/hidkbdmap.c definitions. This is an
ASCII console map, not an IME or a complete wscons keyboard implementation.

Repeat starts after 500 ms and runs at approximately 33 ms intervals, rounded
to kernel ticks. It uses the current modifier report. Key release, rollover,
deactivation and detach cancel repeat. A lock protects report/repeat state;
detach closes the HID pipe, waits for callbacks/callouts and destroys the
callout/lock before the device can be freed. A replacement starts with fresh
Caps/held-key state. OHCI shutdown also stops its completion-poll callout so
it cannot restart after the host controller is reset.

The extended native_usb_host_test.py compiles production DMA mapping and
keyboard logic with mocked kernel/callout interfaces. It checks JIS/US/Ctrl
punctuation, repeat delay/interval, changing modifiers, multiple held keys,
release, duplicate slots, rollover, deactivation, detach and replacement.
This checks state transitions and cleanup ordering, not actual USB unplug
or multiprocessor concurrency. Real root-hub disconnect/re-enumeration remains
unverified; unplugging the host keyboard alone does not remove PCSX2's
emulated Konami USB device.

Initial run40 exposed a separate PCSX2 input issue: its Qt symbol-to-key
normalization maps shifted punctuation using US positions. A real JIS host
keyboard could therefore send Shift+Apostrophe for a double quote, which a
correct guest JIS map renders as an asterisk. Changing the guest keymap alone
cannot preserve the host's physical positions through that transformation.

The specified PCSX2 fork now provides an opt-in device setting:
Physical Keyboard Input (Windows), under HID Keyboard (Konami). In the test
profile, USB1 has hidkbd_PhysicalKeyInput=true. It sends native Windows set-1
positions through the existing QEMU HID queue, bypassing this device's
logical Keyboard binding. Default behavior remains available with the option
off; other frontend shortcuts keep their existing behavior. The guest still
selects the keyboard layout. This does not pass an actual USB device through
WinUSB/libusb. E0 native variants and JIS Ro/Yen/Kana/Henkan/Muhenkan usages
are handled; E1/Pause is not supported by this new path.

The user manually confirmed correct JIS punctuation in run41. The first
focus-clear implementation then failed to stop guest repeat: clearing the
HID queue made hid_has_events() false, so the endpoint NAKed instead of
sending key release. The final implementation clears held keys/modifiers
and leaves a queued no-op until an all-released report is delivered. It also
observes application deactivation and focus-window changes. In run42 the
user confirmed repeat stops when leaving the PCSX2 window and normal input
works on return. Both manual runs have zero-byte serial.ee-sio.rx.bin files;
no SIO CLI input or Computer Use was used for those confirmations.

PCSX2's tests/test_usb_keyboard_physical.py compiles production native scan
normalization, physical-state methods, QEMU key mapping and HID queue/report
processing. It passes JIS/modifier/duplicate/focus-release tests, including
a previously full queue. This host test does not synthesize Qt focus events;
the focus behavior was checked manually. Details are in the PCSX2 tree's
tools/usb-physical-keyboard.md. Its existing unrelated local changes were
preserved. Native keyboard, OHCI shutdown, PCSX2 physical input and validation
records are maintained as separate commits.

Both run39 (before input hardening) and run43 (final keyboard/shutdown code
with the updated PCSX2 executable) booted with the USB keyboard attached and
passed the 80 MiB memory test, JIT code rewrite/cacheflush, fork/COW isolation,
normal/delay-slot fault EPC/BD checks and the EE full-context test. Run43:

```text
RAM128: resident=10240 pages (80 MiB), swap=0, maxrss=82520 KiB
RAM128: ALL CHECKS PASSED
mem-status=0
JIT regression: ALL CHECKS PASSED
jit-status=0
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
SBIOS unavailable; CPU halted.
```

Run43 consumed the 92 test-input bytes, saved all 4356 EE SIO output bytes,
reported no observation fault, completed filesystem shutdown and was stopped.
It used EE JIT, Full TLB, EE cache, 128 MiB, IOP interpreter and software GS.
Cache, SPL, native SIF and DMA/HID host checks, R5900 TLS/ISA checks for sh/init,
and the final kernel/root build passed.

Final normal PS2USB ELF SHA-256:
5fe032ee1e80fc4b2784d8af6a43cf719c54e8994db0eb0fee2153de9fc576a5
Final PS2USB regression ELF SHA-256:
baf425844ff721414eaf5d5e9815dd96acc3cbbcf50479a5c46d09a8099ab5f6
Updated PCSX2 EXE SHA-256:
af36998a36ae212b2178e983765122d8a53f8a6ebb47404ec5425dab93a52ded

### IOP interrupt forwarding: module-start capability still unresolved

The existing BSD SBUS interface uses the USB flag 0x400 and EE INTC channel 1.
The specified emulator sends OHCI IRQs to IOP IRQ22, not directly to the EE.
A real IOP handler is needed to mask the OHCI source, publish a notification
and signal the EE; the EE must service the controller and safely rearm it.
Simply enabling an EE interrupt or removing the tick poll is insufficient.

An independent BSD-2-Clause experiment was compiled using the installed IOP
GCC/binutils and the ps2-irxgen host tool, with no SDK headers, libraries or
prebuilt modules linked into the result. Public import ordinals, IRX metadata
and LOADFILE wire fields were consulted as interface facts; this is not a
claim of clean-room development. The no-argument probe only sets an execution
marker and returns nonresident, without installing an IRQ handler.

Run44 used a separate temporary PS2USBIRQPROBE kernel to bind resident-ROM
LOADFILE service 0x80000006 and request operation 6 with the module in IOP RAM:

```text
ps2sif0: IRX probe: transport=0 module=-1515870811 result=-1515870811 executed=0 SMFLAG=0x30000
```

Both reply words retained their 0xa5a5a5a5 sentinel, and the module's execution
marker was absent. This confirms neither successful module loading nor
execution; transport completion alone is insufficient. It suggests that the
current ROM service does not supply this memory-buffer operation, but packet
or loader compatibility still requires investigation. The USB shell still
booted afterward. No ROM patch or SDK loader patch was applied.

The experiment sources, IRX, generated blob, temporary kernel config and
kernel-probe.patch are saved under build/ps2/irq-experiment (ignored build
artifacts, outside the committed source set). Temporary probe hooks/config
were removed from the normal source tree after capture. The normal PS2USB
ELF remains the regression-tested polling build. Independent module loading,
actual IOP-to-EE forwarding, poll reduction and real USB hotplug verification
remain outstanding; none is claimed complete by this milestone.

## 2026-10-18: independent IOP module startup and PCSX2 Keyboard

The preexisting PCSX2 work was committed before proceeding: c0867d11a
(AVX-512 PCH/build hygiene), 7d4c93c62 (RAW AgentConsole), f4c5ababe
(distribution notices), 2e63feefa (development references), f7c69d2fe
(Python cache ignore and integration status). No push was performed.

### Resident ROM buffer loading

The operation-6 failure above is now explained. The tested resident LOADFILE
dispatcher starts with `sltiu v0,a0,6` and rejects operations >= 6. Its
successful transport completion did not mean it accepted a buffer-loaded
IRX. Run49's independent callback experiment subsequently loaded and ran a
module, observed its execution marker, and restored the original callback.

The source integration is opt-in as PS2IOPBOOT, derived from PS2USB:

```sh
KERNEL=PS2IOPBOOT sh build-ps2-shell.sh
```

See sys/arch/playstation2/iop/README.md and build-ps2-iop.sh. The source set
includes a relocation-free bootstrap, nonresident module, import directory,
linker metadata, and wire ABI. No SDK headers, libraries or prebuilt modules
are included or linked; installed IOP compiler/binutils and the ps2-irxgen
host tool generate the images. Public ABI declarations were consulted as
interface facts; this is not a clean-room claim.

The EE discovers unique loadcore/modload exports rather than hardcoding ROM
function addresses. Under exclusive RPC ownership, it temporarily replaces
the LOADFILE RAM callback with our bootstrap's uncached IOP alias. That code
flushes I-cache, loads the IRX and starts it with a mailbox argument. No ROM
instruction is patched. The original callback and heap binding are restored
before releasing staging memory. An uncertain completion retains memory and
disables further RPC use to avoid freeing potentially executing code.

Run50 used the integrated path and reported:

```text
ps2sif0: independent IOP module: error=0 id=25 entry=1 start=25 mailbox=OK signature=0x42524745
```

The nonresident module also called intrman suspend/resume imports. Its four
mailbox words, twelve guard words, entry result and reply signature passed.
USB keyboard enumeration and the RAM-root shell followed normally.

Run51 repeated module startup with the regression root and passed resident
80 MiB without swap, JIT rewrite/cacheflush x64, fork/COW, normal/delay-slot
EPC/BD, and GPR128/HI/LO/HI1/LO1/SA across syscall/timer/sleep/signal/fork.
Both exit statuses were zero; filesystem shutdown and CPU halt completed.
It consumed 92 input bytes, saved all 4477 EE SIO output bytes and reported
no observation fault. The normal PS2USB configuration also still builds.

Host tests cover production transaction bounds, export ambiguity, callback
restoration and timeout allocation lifetime. A freestanding 32-bit host build
of the actual bootstrap verifies its wire ABI and flush/load/start ordering,
including load failure. Native SIF and USB DMA/HID tests also passed.

Normal PS2IOPBOOT ELF SHA-256:
dd2118ff8a2bd9345b7b8fce9f9970c961164f1df639353fe60625c6e461554b
Regression PS2IOPBOOT ELF SHA-256:
aec9fe5b11566efc52c81f92011e83455abb49638f5c4beaf47d8e309570e291

This remains dependent on resident ROM service ABIs and exclusive EE RPC
ownership, and has only been tested on the specified PCSX2 fork. IOP reset,
IOP IRQ22-to-EE forwarding, reduced USB polling and actual root-hub hotplug
remain outstanding. The smoke module installs no interrupt handlers.

### Dedicated PCSX2 Keyboard device

At the user's request, physical-key delivery moved from the Konami option
to an independent PCSX2 Keyboard device (`Type=pcsx2kbd`). Both the settings
device list and guest USB strings use PCSX2 Keyboard. Konami remains the
original logical-binding device; its old PhysicalKeyInput option is removed.
The new virtual device uses development VID/PID 0000:0001, not Konami's ID.
Its JISLayout setting defaults to true and reports HID country 15; US mode
reports 33. The existing NetBSD country-code detection needs no new quirk.

The Windows device uses the already-tested native key and focus-release
path automatically. Other platforms retain a logical binding fallback.
Factory tests compile production creation code and check both identities,
JIS/US descriptors and Windows/non-Windows input isolation. Existing physical
scan/JIS/modifier/duplicate/focus-release report tests pass. Clang AVX2 builds.

Run52 with the rebuilt EXE and normal PS2IOPBOOT ELF reached the shell with:

```text
uhidev0: PCSX2 (0x0000) PCSX2 Keyboard (0x0001), rev 1.10/1.00, addr 2, iclass 3/1
ps2kbd0 at uhidev0: boot keyboard to direct console (JIS ASCII, repeat 500/33 ms)
```

It retains EE JIT, Full TLB, EE cache, 128 MiB, IOP interpreter and software
GS. No Computer Use or synthetic keyboard input was used for this run; the
earlier manual typing/focus confirmations apply to the shared input path.
PCSX2 EXE SHA-256 for run52:
5bd271d69762f4d3979e3de94ab95bef4fe74a8e6d07d97fe41dacc6bbaf159f

## 2026-10-18: native resident IOP USB IRQ relay

PS2IOPBOOT now loads the independently authored `iop/usb_irq.c` after the
nonresident smoke test and OHCI initialization. The module registers IRQ22
with intrman, masks OHCI MIE on an interrupt, publishes a sequence/count in
uncached IOP RAM, sets SMFLAG 0x400, and signals EE INTC channel 1 through IOP
0x1f801450 bit 1. Register semantics were checked in the existing NetBSD SBUS
driver and this user's PCSX2 IopMem/IopHwWrite/HwWrite implementations. No
PCSX2 code was changed for this relay.

The EE handler is established before module start. Magic and twelve guard
words gate activation, and an initial software notification tests the path.
The EE consumes OHCI status, acknowledges the sampled sequence and restores
MIE. It does not issue RPC from interrupt context. The ordinary tick poll
remains; it also checks for unacknowledged sequences to recover a lost edge.
Stop prevents rearm, masks MIE, removes the EE handler and retains the resident
IOP code/mailbox until reset, including uncertain module-start failures.
Detach disconnects children and stops interrupts/polling before freeing OHCI
state. Relay restart after stop is deliberately unsupported in this boot.

Runs54-56 returned successful resident module entry but failed mailbox magic
validation: magic=0, sequence=1, guards intact. The generated code reused an
R_MIPS_HI16 with a later standalone R_MIPS_LO16. Compiling with
`-mno-explicit-relocs` produced fresh pairs and resolved the observed ROM
relocation failure. Both generated IRXs now undergo paired-relocation and
program-header checks in `build-ps2-iop.sh`. Run53's sandboxed launcher timed
out before RAW capture; its process was absent at cleanup. Subsequent runs
used the same specified EXE through AgentConsole outside that sandbox.

Run57 reached the USB keyboard shell with the relay active. Run58 passed the
80 MiB, JIT/cacheflush/fork-COW/EPC-BD and complete EE integer-context tests,
then shut down with IOP=22, SBUS=23, recovered=0, seq=ack=23. The extra SBUS
notification is the software startup probe, not a hardware IRQ22 entry.

Run59 tested the final callback-stop and detach implementation. RAW output:

```text
ohci0: IOP IRQ22/SBUS relay active (tick fallback retained)
ps2kbd0 at uhidev0: boot keyboard to direct console (JIS ASCII, repeat 500/33 ms)
RAM128: resident=10240 pages (80 MiB), swap=0, maxrss=82520 KiB
RAM128: ALL CHECKS PASSED
mem-status=0
JIT regression: code rewrite/cacheflush x64 OK
JIT regression: fork/COW code isolation OK
JIT regression: load exception EPC/BD=0 OK (cause=00000408)
JIT regression: delay-slot exception EPC/BD=1 OK (cause=80000408)
JIT regression: ALL CHECKS PASSED
jit-status=0
PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK
ps2sif: USB IRQ stop: IOP=21 SBUS=22 recovered=0 seq=22 ack=22
SBIOS unavailable; CPU halted.
```

The regression command was accepted at EE SIO output offset 3726. Actual
result lines, not its echo, establish success. Input consumed=92 bytes,
output saved=4621 bytes, observation fault empty. The emulator was stopped
after guest halt. Both runs used the configured JIT/Full TLB/cache/128 MiB,
IOP interpreter, software GS, and PCSX2 Keyboard. No Computer Use or generated
USB key events were used.

Host tests compile production EE/IOP code to check registration failure,
resident entry, mask/notification, duplicate/lost edges, rearm races, callback
stop, guards and timeout retention (`tests/ps2/usb_irq_host_test.py`). Bootstrap,
native SIF/USB, R5900 cache/SPL and userland ISA checks also pass. Both
PS2IOPBOOT and PS2USB build. A real controller detach, root-hub unplug/replug,
manual key input on this version and real hardware remain untested. Keeping
the tick poll means this does not yet measure latency/load improvement or
prove full USB operation with only IRQ completions.

The run artifacts (kept outside the tree) include
`run59-usb-irq-final/serial.ee-sio.bin` as the final evidence.
SHA-256 of the final build:

```text
netbsd-PS2IOPBOOT.elf
3e36827cbe8063bb292e4c1dba47cca318b3f6a25e8560b5de0917cbf157338a
netbsd-PS2IOPBOOT-jit128.elf
3046551a963ed3ea1247b3801a8a7488d66b55236dfdcd51f7d8d21f4c01c8d3
```

## 2026-10-18: one-second OHCI watchdog and polling comparison

Committed the independent loader/resident USB IRQ implementation as
`db3fe35991c`. The subsequent change uses a one-second OHCI watchdog when the
relay is active, preserving one initial tick to recover a startup notification
that arrived during synchronous module loading. If relay establishment fails,
or the relay is stopped/corrupt, the attachment schedules its original
per-tick polling. An IOP stall which leaves mailbox magic/guards intact is
still covered by the one-second status poll, with corresponding possible
fault-recovery latency. This is not a runtime IOP heartbeat protocol.

The attachment counts nonzero `ohci_intr` returns separately in IRQ and poll
context, plus poll invocations. The poll callback runs at IPL_BIO so a nested
SBUS interrupt cannot be misattributed to polling. Host tests exercise pacing,
health fallback, work attribution and stopping; the existing relay test now
also checks the active-state API. Both PS2IOPBOOT and PS2USB build.

Using the same specified PCSX2 EXE/profile and regression command as run59:

| Run | Configuration | Stop time (guest) | IRQ work | Poll work | Poll calls |
| --- | --- | --- | --- | --- | --- |
| 60 | PS2IOPBOOT, 1-second watchdog | 19.8399579 s | 22 | 0 | 19 |
| 61 | PS2USB, tick polling | 20.4199565 s | 0 | 21 | 1942 |

OHCI attaches at guest time 1 s, so both observation periods cover about 19 s.
Run60 stopped with `IOP=22 SBUS=23 recovered=0 seq=23 ack=23`; the additional
SBUS notification is the software startup probe. Both runs enumerated PCSX2
Keyboard as JIS and passed 80 MiB resident/swap=0, JIT rewrite x64/fork-COW/
EPC-BD, and complete EE integer-context tests, then unmounted and halted.
This demonstrates enumeration completions handled by IRQ with no watchdog
work in run60, and preserves the ordinary polling path in run61. Scheduled
OHCI polling falls from approximately 100 Hz to 1 Hz. These counters do not
measure input latency or host CPU utilization, and do not prove behavior of
arbitrary USB transfers, hotplug or a mouse.

Run60's command was accepted at output offset 3695, input consumed=92 bytes,
output saved=4659 bytes. Run61 used offset 3572, consumed=92, saved=4458.
Each passed based on actual result lines and halt, not command echo.
Observation faults were empty; both emulator processes were stopped. No
Computer Use or generated USB keyboard reports were used.

Artifacts are in the same Windows directory as run59, with new filenames
`netbsd-PS2IOPBOOT-watchdog.elf`, `netbsd-PS2IOPBOOT-watchdog-jit128.elf`, and
`netbsd-PS2USB-poll-jit128.elf`. SHA-256 respectively:

```text
e630ae9dc1cdc5de54bb2b8a82c61517bfe02ebd349a7f03cd9d869671b99664
2f4db813f51eb7ba3d1ff10a338d548f90002ce37b999b9e3db9f70296d73122
1db98438ecdd534045cd86ced23f323c572c7ed7c1baf33a848dc4da283cad22
```

Next validation is USB root-hub detach/re-enumeration, held-key disconnect,
repeat termination and reconnection, plus manual JIS/focus/input checks with
the watchdog configuration. Physical hardware and actual OHCI controller
detach remain untested.
