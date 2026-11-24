# Independently authored IOP bootstrap

These BSD-2-Clause sources use no PS2SDK headers, libraries, or prebuilt IRX
modules. The installed IOP GCC/binutils compile them, and the `ps2-irxgen`
host executable packages our ELF metadata. Public module ABI facts were
consulted in the local SDK's loadcore/modload/intrman and SIF declarations;
this is not a clean-room development claim.

Build the experimental USB shell with:

```sh
KERNEL=PS2IOPBOOT sh build-ps2-shell.sh
```

The kernel build uses the committed `ps2_iop_images.h`, so `build.sh` needs no
IOP toolchain. After changing the IOP sources, regenerate it with
`PS2DEV=/path/to/ps2dev sh mkimages.sh` (overrides: `IOP_CC`, `IOP_LD`,
`IOP_OBJCOPY`, `IOP_IRXGEN`) and commit the result.

`ps2_iop_images.h` holds only code and data built from these BSD-2-Clause
sources. The modules are linked with `ld` alone (no libgcc), and
`ps2-irxgen` only repackages the ELF: the `.text`, `.rodata`, `.data` and
`.iopmod` contents of each IRX are byte-identical to the linked ELF, whose
`.iopmod` comes from `module.ld`.

The tested ROM LOADFILE dispatcher accepts operations below 6, so it cannot
provide the usual buffer-loading operation 6. The EE instead finds unique
loadcore/modload export tables with valid function pointers, allocates IOP
staging memory, and temporarily replaces the resident LOADFILE server's RAM
callback. Its exclusively owned RPC calls our relocation-free bootstrap
through an uncached IOP address. The bootstrap flushes the I-cache and calls
the ROM LoadModuleBuffer and StartModule functions. ROM instructions are not
patched. This depends on the resident ROM module ABI and assumes there is no
other EE LOADFILE client or concurrent IOP reset.

The original callback and EE heap-service binding are restored on exit.
An uncertain RPC completion retains staging memory and disables subsequent
heap RPC use rather than freeing potentially executing code. The callback
must not escape the synchronous request. Loaded resident modules must own
their allocations independently of the staging buffer.

The nonresident smoke module parses a bounded mailbox argument, calls the
intrman suspend/resume imports, and writes four signature words. The EE also
checks twelve guard words, the module entry result, and the RPC signature.
It installs no interrupt handlers. A separate resident `usb_irq.c` module
provides the IRQ22-to-EE SBUS relay in PS2IOPBOOT. PS2USB retains polling.

The resident module registers intrman IRQ22 in mode 1. On entry it disables
OHCI MIE, increments a shared sequence and hardware-entry count in uncached
IOP RAM, sets SMFLAG bit 0x400 (IOP 0x1d000030), then sets bit 1 of IOP
0x1f801450 to signal EE INTC channel 1. These register paths were checked
against this tree's SBUS driver and the specified PCSX2 fork's IopMem,
IopHwWrite and HwWrite implementations. Other SMFLAG bits are preserved.
The native profile owns INTC channel 1 exclusively; it must not also attach
the SBIOS-backed sbus driver.

EE installs its handler before loading the module. A startup notification
and magic/guard validation precede OHCI rearm. EE services OHCI, acknowledges
the sampled sequence, and restores MIE. The IOP never rearms the device or
acknowledges OHCI status. A later device event remains pending in OHCI and
asserts IRQ22 on rearm. Sequence comparison recovers a missed SBUS edge;
duplicates do not rearm twice. Interrupt context performs no RPC or allocation.
After one initial tick to recover the startup notification, an active relay
uses a one-second OHCI watchdog. Unavailable/stopped relays use the original
per-tick poll; a mailbox guard failure switches back at the next watchdog.
Shutdown reports IRQ versus poll work (nonzero `ohci_intr` results) and poll
call counts. Attribution runs at IPL_BIO to exclude a nested SBUS interrupt.
The watchdog checks missed sequences and also polls OHCI status, so an IOP
stall without mailbox corruption can still be serviced once per second.
This is a recovery bound, not a low-latency guarantee under faults.

Shutdown stops polling/rearm, masks MIE and removes the EE interrupt handler.
Detach disconnects children, stops the relay and callout, and then releases
OHCI state. Resident IOP code and its 64-byte mailbox are retained until IOP
reset, including uncertain module-start timeouts. Stop is terminal for this
boot; controller reattachment cannot install another relay. A stopped module
only masks/notifies if a stale IOP IRQ runs, so it cannot reenable MIE. Mailbox
guard failures disable relay rearm and leave polling available.

Compile with `-mno-explicit-relocs`: the tested ROM relocator did not correctly
handle GCC's reused HI16 with a later standalone LO16. With explicit relocs,
the resident entry returned success and updated sequence, but magic stayed
zero. Fresh HI16/LO16 pairs fixed this in PCSX2. The build checks both IRXs
for paired relocations so changing compiler options cannot silently restore
that failure. This is a compatibility observation for the tested ROM.

Host verification:

```sh
python3 tests/ps2/iop_bootstrap_host_test.py
python3 tests/ps2/sif_native_host_test.py
python3 tests/ps2/usb_irq_host_test.py
python3 tests/ps2/ohci_watchdog_host_test.py
```

The former tests production EE transaction code with mocked RAM/transport,
including bounds, ambiguous exports, callback restoration and timeout memory
lifetime. It also compiles the actual bootstrap as a freestanding 32-bit host
program to test its wire ABI and callback ordering. Runtime confirmation is
limited to the specified PCSX2 fork and ROM; real hardware remains untested.
The USB IRQ test compiles production EE and IOP code with mocked MMIO/imports
to check lost edges, duplicate notifications, rearm races, stop during the
callback, timeout retention, guards, and registration failures.
The watchdog test checks one-second versus tick pacing, health failure,
work attribution, and shutdown. In PCSX2 run60, USB enumeration and the EE
regressions passed with IRQ work=22, poll work=0 and 19 poll calls over about
19 guest seconds. The PS2USB baseline (run61) passed with IRQ work=0, poll
work=21 and 1942 poll calls over about 19 guest seconds after attach. Manual
typing/latency and real hardware are not validated by those counts.
