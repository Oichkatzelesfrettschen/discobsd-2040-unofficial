# Historical porting and operations mechanisms for the RP2040 port

Three documents from the DiscoBSD and 2.11BSD lineage supply one missing
operational mechanism and several independent checks on mechanisms already in
the tree. The missing mechanism is preservation of local files across a root
image replacement. The existing mechanisms are the separation of emulator and
board evidence, linked-image capacity admission, canonical machine selection,
and the reset-to-init path.

The source PDFs were extracted with `pdftotext -layout`. Their page counts and
SHA-256 digests bind every citation below:

| Source | Pages | SHA-256 |
| --- | ---: | --- |
| Christopher K. Hettrick, *Porting the Unix Kernel*, 30 December 2020 | 20 | `0548ffdd3a3457a9e2b71c8c7213d2f755e428a7e8f3a7e1cc0ec872f5242d4f` |
| Steven Schultz, *Installing and Operating 2.11BSD on the PDP-11*, title dated 13 June 1995 and running revision dated 27 October 1997 | 56 | `a1641603d2bfc848f2f3830c7860e3e8094f62f87901469ea9f4e46eb4158f3e` |
| Bernd Ulmann, *Installing (and running) BSD 2.11 on a PDP11*, 28 December 2011 | 23 | `b1b348c5bd5f1720b78c0b65f00892f577f817862465b661c383c847a1b657b6` |

The target comparison uses tree `077dc24600dbf4668f651c5045cbd5792f4916d8`
on 19 November 2026. Historical claims remain historical evidence. Current
source and gates establish the RP2040 behavior.

## Root replacement needs a preservation boundary

Schultz treats an incompatible filesystem upgrade as a fresh bootstrap. The
operator takes a complete backup and a separate archive of site state, then
uses the old files as merge inputs instead of overwriting the new system
(*Installing and Operating 2.11BSD*, sections 3 and 3.1, PDF pages 16--18).
The inventory distinguishes user data, device construction, disk layout,
accounts, startup policy, terminal policy, scheduled work, logs, spools, local
programs, and the kernel configuration.

The RP2040 image builder creates a new filesystem from the manifest, converts
it into a flash image, and writes the whole 1536 KiB root region. The decisive
source path is:

```text
Makefile:${FSIMG} -> distrib/rp2040/Makefile.inc:${FLASHBIN}
    -> distrib/rp2040/Makefile.inc:${FLASHUF2}
    -> distrib/rp2040/host/discobsd-flash:picotool load -v
```

`distrib/rp2040/Makefile.inc` states why the complete region is intentional:
stale Dhara journal blocks must not survive beside the replacement image.
Loading only `unix.uf2` preserves the root. Loading `flash.uf2` replaces the
root, including user files and edited configuration. The README now names that
boundary at the recovery and flashing instructions.

No shipped host command exports or merges board state. This negative is
bounded to `distrib/rp2040/host`, `README.md`, `sys/arch/rp2040/doc`, and the
RP2040 manifest at the source revision above; `rg -i
'backup|export|restore|preserv'` found release-archive and diagnostic uses but
no board-state workflow. The root does ship `/bin/tar`, `/usr/bin/cpio`,
`uuencode`, and `uudecode`, so the filesystem side can already form and encode
an archive. The host console tools do not yet provide a bounded file-transfer
protocol.

The smallest complete preservation mechanism has four parts:

1. A read-only host command requests a versioned archive through the serial
   console and verifies its digest and terminal record before publication.
2. The archive carries `/home`, `/root`, and the mutable configuration selected
   from the installed manifest. It records `/etc/release` as source identity
   but never restores that file.
3. Restore writes into a staging directory, compares each configuration file
   with the new image, and requires an explicit merge for conflicts. It never
   replaces `/etc/COPYRIGHT`, `/etc/release`, `/etc/rc`, device nodes, shipped
   programs, or libraries from the old image.
4. A fixture changes a home file, an account field, a terminal entry, and an
   `rc.local` line; export, root replacement, and restore must preserve those
   changes while keeping the new release identity and startup script. Truncated
   archives, digest changes, paths outside the allow-list, device entries, and
   an old release file must be rejected.

This design extracts Schultz's invariant -- preserve local state and merge it
against the new system -- without importing PDP-11 tape, disk-label, or raw
device procedures.

## Emulator success remains separate from board success

Ulmann constructs and configures a complete disk under SIMH, saves the disk
container as a checkpoint, and transfers it to physical hardware (*Installing
(and running) BSD 2.11 on a PDP11*, sections 2.5 and 4, PDF pages 10 and
18--21). The physical machine boots the image, but its log reports
`/dev/swap: Operation not supported by device` on PDF page 21. The image moved
successfully while one configured service failed under the physical device
path. That result is a concrete counterexample to emulator boot implying full
hardware operation.

The RP2040 tree already applies the correct evidence split. `check-renode`,
`check-renode-picocalc`, and the SD-root Renode gates boot specific generated
images under pinned third-party models. `sys/arch/rp2040/doc/TESTING.md` keeps
those results separate from physical-board results. The historical case
supports that boundary and supplies no reason to weaken it.

The transfer handoff still has one useful improvement. `discobsd-flash`
verifies each UF2 while loading it, while `boardinfo` exposes the running
kernel image length and CRC, `/etc/release` exposes the root identity, and the
mount table exposes the selected root. A future verified-install command can
wait for the console after reboot and compare all three with the intended
bundle. Success then means that the requested board runs the requested kernel
and root, not only that `picotool` accepted their bytes. A known-bad kernel
CRC, stale root release, and wrong root device calibrate the three refusals.

## Linked capacity, not source size, admits a kernel

Ulmann's configured PDP-11 kernel fails at link time with `ld: too big for
type 431`; the repair manually moves object files between the base segment and
8 KiB overlays until every placement constraint holds (section 3.1, PDF pages
14--16). Schultz likewise warns that a generated kernel Makefile can fail when
its base or an overlay has the wrong size (section 4.2.3, PDF page 25).

RP2040 execute-in-place flash removes the PDP-11 overlay mechanism, but it
keeps the capacity invariant. `tools/check_kernel_headroom.py` measures every
flash-backed `PT_LOAD` span, including `.data` and `.ramfunc` initializers,
against the linker region and a margin. Its self-test admits boundary cases
and rejects overflow, malformed ELF files, truncated payloads, overlapping
segments, and missing flash loads. `check-kernel-headroom` applies the
calibrated checker to every built board kernel. The historical mechanism is
therefore already superseded by a target-specific gate with a stronger oracle.

## Portability variables now form one canonical tuple

Hettrick records `MACHINE` and `MACHINE_ARCH` as compulsory independent
environment variables used to select MIPS or Arm builds (*Porting the Unix
Kernel*, section 10.1, report page 13, PDF page 17). Independent selectors can
describe an impossible target.

`share/mk/architecture.mk` now treats `MACHINE` as the public selection and
derives `MACHINE_ARCH`, `MACHINE_CPU`, and `MACHINE_ARCH_FLAGS` from the
maintained registry. `tools/architecture-isolation/check-architecture-isolation.sh`
calibrates the boundary with unknown machines, split machine/architecture and
machine/CPU pairs, and attempts to replace private registry values. Hettrick's
multi-architecture aim survives; the current registry closes the inconsistent
tuple that the original interface admitted.

## Reset-to-init structure survives with a different load mechanism

Hettrick traces reset, linker placement, machine startup, `main()`,
`newproc()`, the swapper, copied `icode`, `/sbin/init`, `getty`, `login`, and
the shell (*Porting the Unix Kernel*, sections 6 and 7, report pages 7--11,
PDF pages 11--15). That control structure remains recognizable in
`sys/arch/rp2040/rp2040/locore0.S`, `sys/arch/rp2040/rp2040/machdep.c`, and
`sys/kern/init_main.c`.

The storage transition differs. Hettrick describes reset code loading the
kernel from flash into RAM. RP2040 boot2 configures the execute-in-place (XIP)
window and transfers control to kernel text in QSPI flash; only initialized
data, RAM functions, and uninitialized storage occupy SRAM. The current
authority is `sys/arch/rp2040/doc/BOOT-MAP.md`, which binds the ROM rule,
boot2 checksum, vector address, linker regions, user transition, and first
observable console lines. The report supplies lineage for the control path,
not the RP2040 placement rule.

## Original Arm-port work items map to current RP2040 mechanisms

Hettrick's future-work section names a syscall ABI, userland and libc, UART,
SPI-attached SD storage, GPIO, and memory protection as unfinished work
(*Porting the Unix Kernel*, section 12, report page 14, PDF page 18). The
current RP2040 tree contains each mechanism:

| 2020 work item | Current mechanism | Current evidence boundary |
| --- | --- | --- |
| syscall ABI | `sys/kern/syscalls.master`, generated dispatch tables, SVC/PendSV entry under `sys/arch/rp2040/rp2040` | metadata, relocation, cross-build, Renode, and board gates remain separate |
| userland and libc | root manifest, Arm libc, a.out loader, native assembler/linker/compiler closure | a built image proves composition; program gates prove only their workloads |
| UART | `sys/arch/rp2040/dev/uart.c` and the PICO UART configuration | Renode covers its PL011 model; board runs cover measured silicon |
| SPI SD | `sys/arch/rp2040/dev/sd.c`, PicoCalc configuration, and SD-root image | SD-root geometry and Renode boot do not establish a physical card run |
| GPIO | `sys/arch/rp2040/dev/gpio.c` and `sys/arch/rp2040/doc/GPIO.md` | request and build checks remain separate from pin voltage measurement |
| memory protection | `sys/arch/rp2040/rp2040/mpu.c` programs and reads back four regions; `mputest` deliberately faults forbidden access | register readback and the deliberate fault are required before a protection claim |

The source searches behind this table were `rg` for `syscalls.master`,
`newproc`, `icode`, `SVC`, `PendSV`, `device uart`, `sdrootattach`, GPIO driver
registration, `mpu_init`, and `mputest`, followed by direct reads of the
matched source and owning documents. The table records implementation
presence. It does not promote a build, Renode run, or historical report into
current board evidence.

## Historical practices that stay historical

The three documents also carry mechanisms whose assumptions do not fit the
RP2040 port:

- PDP-11 disk labels, tape files, bad-sector forwarding, raw block devices,
  and boot blocks describe a removable-disk installation contract. RP2040
  flash uses a fixed linker and Dhara layout enforced by storage gates.
- PDP-11 kernel overlays solve a 16-bit address-space placement problem.
  RP2040 kernels execute from a fixed QSPI window and use linked-span
  headroom admission.
- Splitting root, `/tmp`, `/usr`, user data, and swap across disk arms reduces
  seek contention in Schultz's workload (section 4.3.2, PDF pages 26--27).
  The Pico has one QSPI flash device; the PicoCalc adds SD and serial PSRAM
  with different bandwidth, endurance, and ownership contracts. Current
  placement therefore follows measured device costs rather than disk-arm
  rules.
- Hettrick describes simulators and emulators as cycle-accurate in section 4.
  That statement supplies no timing authority for Renode. The current gates
  use Renode for the peripherals and instruction paths they exercise and
  reserve timing and silicon claims for the board.

The surviving content is the invariant under each historical procedure:
preserve local state before replacement, identify the image at every handoff,
admit the final linked layout, and keep emulator and physical results in their
own evidence classes.
