# A Thumb-1 (ARMv6-M) QEMU test tier

Drafted with assistant participation (Claude, Sonnet 5.5); every number below
comes from a command run in this tree unless it names a URL.

## Answer

1. A strict ARMv6-M user-mode CPU already exists. `qemu-arm -cpu cortex-m0`
   raises SIGILL on a Thumb-2 instruction and SIGBUS on an unaligned word
   load; qemu-arm's default model accepts both. Every existing qemu-user gate
   (libbn, libcalc, calc) passes on it unchanged. The tier now selects it
   through `QEMU_ARM_CPU` in `tools/qemu-arm.mk`.
2. Three more host gates gain a Thumb-1 execution at a cost of one Makefile
   section: the libc `strtol`/`strtoul`, `random` and `printf` contracts in
   `tests/libc_contracts`. They run the host tests unchanged against the
   tree's own sources compiled with the target's unsigned `char`, and a
   mutant calibrates each. They are in root `check-qemu`.
3. Running the real DiscoBSD user a.out under qemu-user is feasible and
   prototyped (`tests/qemu_aout`). A freestanding loader of about 700 lines maps the
   OMAGIC image at 0x20000000, builds the kernel's stack layout, rewrites each
   `svc #N` site to `udf #N`, serves the resulting SIGILL/SIGTRAP from the
   Linux kernel, and models the boot ROM float tables at address 0. A probe
   program (stdio, malloc, division, single and double arithmetic, exit
   status) and the tree's on-device `tests/rp2040/fptest` both pass. Recommendation:
   adopt it as a user-space ABI gate when the next owner of the loader is
   named; keep it outside root `check-qemu` until a gate needs it.
4. No mainline QEMU machine runs the PICO kernel, and `-cpu cortex-m0`
   cannot be forced onto mps2-an385/an386 (they accept only their own CPU) or
   onto `-M none` (rejects M-profile CPUs). An out-of-tree RP2040 machine,
   `raspi-pico`, exists as a 36-patch RFC (version 3, September 2026) with a
   public fork; its memory map, PL011 UART0, XIP flash, SIO, MPU and a
   synthetic boot ROM match what the PICO_UART kernel needs on paper. Nothing
   in this report booted the kernel on it. Recommendation: schedule a bounded
   experiment (section "Whole-kernel emulation") as a fourth execution class
   beside Renode, outside `check`, and do not rely on it before the experiment
   boots a shell.

## Environment

| item | value |
| --- | --- |
| tree | branch `picocalc/qemu-tier` at `d31d6228`, uncommitted changes |
| qemu-arm, qemu-system-arm | 11.1.3 (`qemu-arm --version`) |
| compiler | arm-none-eabi-gcc 16.2.0, newlib 4.6.0, multilib `thumb/v6-m/nofp` |
| host | Linux 6.18 on WSL2 x86-64, `mmap_min_addr` 65536 |
| interpreter | `PYTHON=/usr/bin/python`, `TMPDIR` outside `/tmp` |

## Evidence classes

The new gates are qemu-user evidence: they execute Thumb-1 instructions under
the emulator's CPU model through the Linux syscall seam. They do not establish
RP2040 bus, MPU, boot ROM arithmetic, DiscoBSD kernel or peripheral behavior;
`sys/arch/rp2040/doc/TESTING.md` keeps those in the cross, Renode and board
classes.

## Question 1: gate inventory and ranking

`bmake -V` counts 76 entries in `HOST_GATES`, 15 in `HOST_PROGRAM_GATES` and
36 in `CROSS_CONTRACT_GATES`. The cross gates compile and inspect; none
executes, so a qemu variant belongs to the host gate each pairs with. The
host gates split three ways.

Gates that execute no C of the target's (nothing for qemu): the Python and
shell policy, ledger and generator checkers (`check-architecture-isolation`,
`-agent-instructions`, `-uf2-validator`, `-run-logging`,
`-battlestar-generation`, `-graft-cache-reader`, `-sus-discovery`,
`-sus-requirement-corpus`, `-dpucp-profile`, `-dpucp-donor-ledger`,
`-dpucp-board-acceptance`, `-c17-source-denominator`, `-capacity-corpus`,
`-comment-hygiene`, `-text-hygiene`, `-warning-policy-host`,
`-build-failure`, `-analysis`, `-test-execution`, `-kernel-metadata`,
`-config-generated-sync`, `-config-include-order`, `-config-makefile`,
`-fs-profiles`, `-storage-correctness` (a Python script), `-root-noatime`
(a Python script), `check-tail-host`'s manifest steps).

Gates that compile one utility source and drive it against files through
a shell script or a C driver (`check-cat-contracts`, `-colrm-`, `-unifdef-`,
`-rmdir-`, `-tee-`, `-touch-`, `-du-`, `-find-`, `-resize-contracts`,
`check-getty-contracts`, `-libc-tempfiles`, `-libc-environment`,
`-tiny-utility-multicall`, `-portable-utilities`, `-id-aliases`,
`-fs-stress`, `-fgrep-capacity`, `-swapram-evac`): their cross twins already
check that the object imports no division helper. A qemu-user variant of the
C driver would need `stat`, `mkdir`, `unlink`, `opendir` and `fork` that
`tests/libbn/qemu_support.c` does not provide. The better home is the a.out
loader below, which would run the shipped utility binary on host files.

Gates whose C runs on the host and whose result depends on the machine
(ranked below).

Gates whose C runs on the host and whose result depends on the machine
(ranked below). The "differs" column names what x86-64 or x86 `-m32` computes
differently from arm-none-eabi-gcc at Thumb-1 ILP32 soft-float: `char` is
unsigned on the target; the EABI aligns `long long` and `double` to 8 bytes in
structures where i386 aligns to 4; an unaligned access faults on ARMv6-M;
division and 64-bit multiplication call libgcc or tree helpers; the host
`-m32` build uses x87 or SSE where the target uses IEEE soft-float.

| rank | gate (directory) | what it executes | differs | cost | per-gate blocker |
| --- | --- | --- | --- | --- | --- |
| done | `check-libcalc`, `check-libbn`, `check-calc-host` | libcalc, libbn, calc line interface | soft-float, 64-bit helpers | exists | none |
| done now | `check-libc-host-contracts` strtox, random, printf (`tests/libc_contracts`) | tree `strtol.c`, `strtoul.c`, `random.c`, `doprnt.c`, `flsbuf.c`, `snprintf.c` | unsigned `char`, 32-bit multiply-add, `long long` printf helpers | implemented | none after the link shim |
| 1 | `check-kernel` (`tests/kernel`, 42 C files, 35 ILP32 receipts in `tools/test-execution-inventory.json`, such as inode, block, sysctl, syscall-arguments, usercopy-span, sigreturn-context, elf-admission) | production kernel files over shims | `long long`/`double` alignment in `struct` layout, unsigned `char`, unaligned access | medium: one rule per program, newlib link, drop sanitizer flags | `ptrace`, `setitimer` and `ioctl` programs name kernel handlers, not host syscalls, so they compile; sanitizer-only variants stay host |
| 2 | remaining `tests/libc_contracts`: mktime, ctime, zone, difftime, asctime, qsort, malloc, scanf, rwmode, vis, string-security, array-memory, line-reading, runtime-limits | 32-bit `time_t` and size arithmetic, allocator over `sbrk`, scanner | `long` overflow, `unsigned char`, signed shifts | low each, same pattern as the three added | `ansi` links a host `fcntl` shim (needs a newlib or Linux EABI replacement); `alarm` and `waitpid` use `fork`, `setitimer`, `alarm` and wall-clock timing; `syslog` writes to a socket |
| 3 | `check-usb-*` (7), `check-dhara-metadata`, `check-dhara-amplification`, `check-flash-device-bounds`, `check-rp2040-usercopy-span`, `check-namei-user-path` (`tests/rp2040/*`) | USB request parsing, Dhara journal and map, flash page arithmetic | byte packing and unaligned access on `cortex-m0` | medium: each `check.sh` compiles `${CC}` with ASan/UBSan and embeds a mutation step, so the qemu variant needs an unsanitized ARM compile plus `QEMU_ARM_RUN` | sanitizer flags hard-coded in the scripts; `namei_path` and `usercopy_span` also run Python checkers |
| 4 | `check-aout` (`tests/aout_header`), `check-picocalc-host` (`tests/picocalc`, the console terminal and key map), `check-libc-casefold`, `check-libc-output`, `check-libc-sysctl`, `check-sysctl-status` | `exec_aout.h` admission predicate, `strcasecmp`, libc output, sysctl library | `char` signedness (casefold already runs both on the host), 32-bit wrap | low each | `libc_output` and `sysctl_status` read the host's `stat`/sysctl through shims |
| 5 | `check-dd-contracts`, `check-umount-contracts`, `check-backgammon-contracts`, `check-printf-contracts`, `check-recovery-utility-contracts` (ILP32 receipts) | utility sources at ILP32 | 32-bit `off_t` wrap | medium | `dd` uses `fork`, `mkstemp`, `waitpid`; `umount` calls `statfs`/`mount` shims; `backgammon` forks and pipes; qemu-user can fork and signal but the shims need Linux EABI structures |
| 6 | `check-timeout-contracts`, `check-libtcl-contracts` | process control, signals, heap overflow under sanitizers | none beyond ranks 1 to 5 | high | `sigaction`, `fork`, `alarm`, `pthread`; libtcl needs sanitizers to reach a verdict |
| 7 | `check-dirent-contracts`, `check-getwd-lifetime`, `check-scandir-contracts` | `opendir`, `scandir` over the host file system | `struct dirent` layout (host, not tree) | high | the host's directory API is the subject; newlib has none |
| 8 | the program gates `check-stevie-host`, `-kilo-host`, `-menu-host`, `-keen-host`, `-bubble-host`, `-fifteen-host`, `-fourfall-host`, `-sort-host`, `-tail-host`, `-fgrep-capacity`, `-compress-host`, `-cpio-host`, `-tar-host`, `-textbox-host`, `-sh-editor` | whole programs against host oracles | game and compression arithmetic | high | editors and games run through a pty (`termios`, `openpty`, `fork`); `textbox` makes FIFOs; tar and cpio interoperate with host archivers; sort and tail compare against host `sort` and `tail` |

The inventory counted C files, process and terminal API use, and Makefile
ILP32 mentions per directory with `grep`; the cost column is an estimate from
the Makefile shape, not a measurement. Ranks 1 to 3 hold the value: they are
the code whose result depends on struct layout, `char` signedness, 32-bit
arithmetic and alignment, and the first two have an ILP32 host run that
already fixes `size_t` width, so the new information is the EABI alignment,
the unsigned `char`, the soft-float helpers and the alignment fault.

### What the three implemented gates found

The strtox test passes with the target's unsigned `char` and with the host's
forced `-fsigned-char`; the random test passes on `cortex-m0`, which the host
`-m32` run cannot assert for unaligned access; the printf contract passes with
`long long` formatting through `doprnt_llong.c`. No defect surfaced. The
gates establish those results for the exercised vectors on the emulator, and
fault detection rests on the calibration below.

### Calibration

Each gate was run against a known-good and a known-bad input before its
verdict counted.

| input | change | result |
| --- | --- | --- |
| good | the tree sources | strtox "51 checks passed", "random contract passed", "printf contract tests passed" |
| bad, strtox | `strtol.c` overflow test replaced by `if (0 && ...)` | "strtol saturates positive overflow and consumes its digits", "1 of 51 checks failed", nonzero exit |
| bad, random | `random.c` line 320 increment 12345 to 12346 | 768 "FAIL random_contract_test.c:107" lines, nonzero exit |
| bad, printf | `doprnt.c` sign character '-' to '+' | "printf contract: hh and h narrow the promoted argument", nonzero exit |
| bad, cpu | `control_thumb2` and `control_unaligned` on `cortex-m0` | status 132 and 135; the same programs exit 0 on `-cpu max` |

A weaker mutant (`cutoff + 1` in the strtol comparison) passed the strtox
test; that is a coverage gap of the host test itself, not of the qemu
variant, and it stays a finding for the owner of `strtox_test.c`.

## Question 2: the real DiscoBSD a.out under qemu-user

### Mechanism

A user program reaches the kernel with `svc #N` and reads failure from the
carry flag (`lib/libc/arm/sys/SYS.h`; `sys/arch/rp2040/rp2040/syscall.c`
takes the call number from the byte before the saved pc and the arguments from
r0 to r3 and the stack). Under qemu-user a Thumb `svc` always selects a Linux
call through r7, so the guest cannot reach a handler by itself. Three
mechanisms were considered:

1. QEMU TCG plugins with a syscall filter callback can skip a call and set
   its return value, but the callback receives the Linux call number (r7 for
   Thumb), not the `svc` immediate, and the carry flag is a CPSR write. Not
   tried; the plugin headers were not inspected.
2. A text scan that replaces `svc` with a branch needs a 32-bit `bl` for a
   16-bit instruction. Rejected.
3. Rewriting `svc #N` (0xDFNN) to `udf #N` (0xDENN) with a loader that serves
   the SIGILL. This is what `tests/qemu_aout/aoutrun.c` does. `udf #1` is the
   Linux ARM Thumb breakpoint, so `exit` arrives as SIGTRAP; the loader
   installs one handler for both signals on an alternate stack, reads r0 to r3
   and the stack from the signal context, runs the call, writes r0, sets or
   clears bit 29 of the saved CPSR and resumes two bytes on.

The svc sites come from `objdump -d` of the linked ELF (a text scan cannot
tell an svc from a literal-pool halfword; a literal 0xDF04 in data would be
rewritten). The loader refuses a site that does not hold an svc.

### Boot ROM model

`lib/libc/arm/gen/rom_float_resolver.S` checks the ROM header at address
0x10 (bytes `M`, `u`, 1, version), the lookup helper halfword at 0x18, the
function-table pointer at 0x14 and the data-table pointer at 0x16, and
requires each target below 0x4000 with the Thumb bit set. qemu-arm refuses to
map guest address 0 unless it has a guest base (`-B`, or `QEMU_GUEST_BASE`;
measured: `mmap(0, ...)` returns EPERM without it and succeeds with
`-B 0x100000`). With a guest base the loader writes the header, a
`rom_table_lookup` stub, the `L3` (clz) function entry and the `SF` and `SD`
tables, each entry a 16-byte stub (`push {r0}; ldr r0,[pc,#8]; mov ip,r0;
pop {r0}; bx ip`) that tail-calls the libgcc soft-float routine for that
operation. The libc glue therefore runs as on the board; the arithmetic is
libgcc's. The ROM's own rounding, NaN and denormal behavior stays a board
measurement (TESTING.md already says so for libcalc).

### Results

| program | result |
| --- | --- |
| `tests/qemu_aout/probe.c` (argv, stdio, `malloc(30000)`, signed division through `aeabi_div.S`, single and double arithmetic, 64-bit to double) | output equals `probe.expected`, exit status 3, on `-cpu cortex-m0` |
| `tests/rp2040/fptest` (the on-device bit-exact float corpus through the ROM glue) | `FPTEST OK` |
| the same probe without `-B` | SIGSEGV at the ROM header read (the float path is exercised through the ROM glue) |
| `tests/rp2040/streamtest` | stops with "89 <- unserved DiscoBSD system call" (`getdtablesize`); the loader names the call rather than guessing |
| hello world | exits with the program's status; the fdtable close sweep at `exit` closes the loader's stdout, which is harmless |

The shipped libc therefore executes on `cortex-m0` without a Thumb-2
instruction or an unaligned access for the paths the probe and fptest cover.

### What breaks or stays out of reach, with effort

| area | state | estimate |
| --- | --- | --- |
| served calls | exit, read, write, open (flag translation), close, lseek (word offset), getpid/ppid, uid/gid, gettimeofday, ioctl (answers ENOTTY), fstat (struct translation), brk | done |
| more file calls: stat, lstat, access, unlink, mkdir, rename, dup, fcntl, getdtablesize, `getdirentries` | struct and flag translation as for fstat | half a day |
| path namespace | DiscoBSD paths need a host root prefix | half a day |
| fork, wait4, pipe | Linux `fork` copies the guest window as BSD fork does; the return-value convention (register pair) must follow `kern_fork` | one day |
| execv/execve | reload the window in the loader and restart | one day |
| signals | `sigaction`/`sigreturn` use the BSD frame (`sig_machdep.c`); the loader must build that frame on the guest stack and map Linux signals | one to two days |
| tty ioctl and `sysctl` (`__sysctl`, `kmemdev`) | terminal modes translate; `machdep.*` and kmem describe kernel state the loader does not have | not worth emulating; stay board or Renode |
| SIO divider | not used: `aeabi_div.S` divides in software; only `usr.bin/mputest` touches SIO | none |
| u-area | kernel-only | none |
| MPU/privilege | not modeled by qemu-user | none |
| `dev` nodes | no `/dev` | out of scope |

The loader checks one thing no host gate checks: that the code the board
runs, linked by the tree's script and converted by `elf2aout`, executes with
the argument convention, stack layout, `crt0` and libc glue in place. It
establishes nothing about DiscoBSD kernel semantics: every call is answered
by Linux. Falsifier: a libc change that alters `svc` argument registers or a
carry-flag convention must move `probe` or `fptest`; the probe was not run
against such a mutation.

## Question 3: whole-kernel emulation

### Mainline machines

`qemu-system-arm -M help` (11.1.3) lists no RP2040 or Pico machine. The M-profile
boards are Cortex-M0 (`microbit`), M3 (`mps2-an385`, `mps2-an511`,
`lm3s811evb`, `lm3s6965evb`, `netduino2`, `stm32vldiscovery`), M4, M7, M33 and
M55 machines. Measured CPU overrides:

| command | result |
| --- | --- |
| `-M mps2-an385 -cpu cortex-m0` | "Invalid CPU model: cortex-m0. The only valid type is: cortex-m3" |
| `-M mps2-an386 -cpu cortex-m0` | "The only valid type is: cortex-m4" |
| `-M none -cpu cortex-m0` | "This board cannot be used with Cortex-M CPUs" |
| `-M microbit -cpu cortex-m0` | starts (native CPU) |
| `-M lm3s811evb -cpu cortex-m0` | starts (not rejected) |

Forcing the CPU on a board without the RP2040 memory map buys nothing: the
kernel is linked for XIP flash at 0x10000000, 264 KB SRAM at 0x20000000 and a
PL011 at 0x40034000, and microbit (nRF51) has flash at 0, 16 KB RAM and a
different UART. QEMU has no generic way to add memory at 0x10000000 to a
machine from the command line. The ARMv6-M instruction rejection the CPU
model provides is already available in user mode (above).

### Out-of-tree RP2040 machines

| project | what it is | license, state |
| --- | --- | --- |
| QEMU mailing list, "[RFC PATCH v3 00/36] arm: add Raspberry Pi Pico/RP2040 machine", 6 Sep 2026: <http://www.mail-archive.com/qemu-devel@nongnu.org/msg1223660.html> | `raspi-pico` machine, continuing Alex Bennee's 2022 RFC; 36 patches rebased on upstream `ff1d2d19d7e2` | QEMU's GPL-2.0-or-later; unmerged; the only review reply read (Peter Maydell, 10 Sep 2026, <http://www.mail-archive.com/qemu-devel@nongnu.org/msg1224705.html>) concerns patch threading |
| <https://github.com/2xs/qemu-rp2040-pico> (branch `rp2040-pico-rfc-v3`; a `rp2040-pico-v4-validation` branch is listed) | the series as a QEMU fork | GPL-2.0 and LGPL-2.1 licenses detected; 2 stars, 1 fork; single-author |
| <https://github.com/2xs/qemu-raspi-pico-tutorial> | Pico SDK examples on the machine | license not stated |
| <https://github.com/wokwi/rp2040js>, <https://github.com/PicoSimulator/PicoSimulator> | non-QEMU emulators | not evaluated; TESTING.md records rp2040js models no flash writes |
| <https://github.com/r0main-f/picoem> | Rust emulator that uses QEMU `cortex-m0` over GDB as its instruction oracle | Apache-2.0 or MIT; personal project |

The cover letter and `docs/system/arm/raspi-pico.rst` (read from the
`rp2040-pico-rfc-v3` branch) describe: two Cortex-M0+ cores with NVIC and
SysTick each, `CONTROL.nPRIV` handling and an M0+ MPU; 16 KiB boot ROM at 0,
264 KiB SRAM at 0x20000000, the XIP window at 0x10000000 with 2 MiB of flash,
UART0 at 0x40034000 and UART1 at 0x40038000 (PL011); SIO with FIFOs,
spinlocks, divider, interpolators and multicore launch; timer and alarms;
clocks, PLLs, oscillators and resets; DMA; an XIP/SSI flash controller with
NOR programming rules and an optional persistent `flash-file`; a synthetic
boot ROM that copies boot2, launches core 1, and offers `rom_table_lookup`,
memcpy/memset, clz, single and double float, and flash helpers. Missing: PIO,
ADC, I2C, PWM, SPI, RTC, SWD, USB packet transactions (a shallow register and
DPRAM layer only), the XIP cache. The series reports 19 RP2040 qtests and 46
check-tcg tests passing. Command line given: `qemu-system-arm -machine
raspi-pico -kernel firmware.elf -serial stdio`.

### Fit to the PICO_UART kernel

| kernel need | machine support (documented) | open question |
| --- | --- | --- |
| XIP flash at 0x10000000, 128 KB kernel then 1536 KB root | yes; `-kernel` loads ELF, UF2 or raw; `flash-file` supplies a raw image | whether `distrib/rp2040/flash.uf2` and `unix.uf2` load as one flash image |
| UART0 console (PICO_UART) | PL011 at 0x40034000 with pin multiplexing | whether `strict-uart-pins` needs `off` for GP0/GP1 |
| USB CDC console (PICO) | register layer only | PICO cannot reach a login; PICO_UART only |
| flash writes through ROM helpers (`sys/arch/rp2040/dev/flash.c`, `machdep.c` call `rom_table_lookup`) | synthetic helpers; the model says they bypass the SSI command sequence | the Dhara root works at the ROM-API level only; SSI timing and command bytes stay Renode and board |
| MPU map (`sys/arch/rp2040/doc/MPU.md`) | M0+ MPU and nPRIV in the series | fidelity to the datasheet's MPU is not shown; `usr.bin/mputest` is the oracle |
| SIO divider | immediate results | no latency model |
| SysTick, NVIC, timer | yes | tick rate vs wall clock |

Renode already boots this kernel to a shell (`check-renode`, outside `check`).
A QEMU machine would add a second emulator with an independent peripheral
model; agreement between two third-party models is not hardware evidence, and
a disagreement is an investigation target, not a verdict.

### Recommended experiment (not run)

Observation: no mainline machine runs the kernel. Hypothesis: the
`raspi-pico` fork boots the PICO_UART kernel to the getty prompt. Falsifier:
the boot stops before "Automatic boot in progress" or hangs after the device
probe lines. Deciding capture: the console log of `qemu-system-arm -machine
raspi-pico -kernel unix.uf2 -serial stdio` with the root image in
`flash-file`. Predicted gate movement: none in `check`; a new opt-in target
`check-qemu-system` that builds a pinned fork revision under a cache directory
and boots, mirroring `tools/renode/check-boot.sh`. Cost: a QEMU build (tens of
minutes) plus the boot and flash-image preparation, estimated one to two
days to a first shell. Pin the fork by commit, record the QEMU base, and
expect rebases while the series is unmerged.

## Question 4: wiring design

Implemented in this tree (all uncommitted):

| file | change |
| --- | --- |
| `tools/qemu-arm.mk` (new) | variables only: `QEMU_ARM`, `QEMU_ARM_CPU` (default `cortex-m0`), `QEMU_ARM_RUN`, `QEMU_ARM_CPUFLAGS`, `QEMU_ARM_CC`, `QEMU_ARM_CFLAGS`, `QEMU_ARM_LIBS`, `QEMU_SUPPORT` |
| `tools/host-test.mk` | includes `qemu-arm.mk`; the values existing consumers read are unchanged |
| `tests/libbn/qemu_support.c` | `c_start` leaves through `_exit` under `QEMU_SUPPORT_EXIT_DIRECT`, so a test that links the tree's stdio avoids newlib's `exit`, which pulls newlib's `fclose` and `fflush` over the tree's |
| `tests/libc_contracts/Makefile`, `qemu_stdio_shim.c` | `check-qemu` builds and runs `strtox_test_arm`, `random_contract_test_arm`, `printf_contract_test_arm`; the shim supplies malloc, free, close, isatty, fstat, write and `__errno` so the tree's stdio links alone |
| `tests/qemu_cpu_model/` (new) | baseline, Thumb-2 and unaligned controls on the tier's model and on `max` |
| `tests/qemu_aout/` (new) | the loader prototype, probe program and expected output; not in root `check-qemu` |
| `Makefile` | root `check-qemu` runs `tests/qemu_cpu_model` first and `tests/libc_contracts check-qemu` last, both under `REQUIRE_QEMU=1` |
| `sys/arch/rp2040/doc/TESTING.md` | two table rows and a paragraph in the qemu tier section |
| `docs/INDEX.md` | this note |

Placement. `check-qemu` is already inside `check`, so `bmake MACHINE=rp2040
logged-check` runs the new gates; no logged-target edit is needed. The CI
owner is `.github/workflows/firmware.yml`, step "Run the qemu tier"
(`qemu-user` from apt; 5-minute step, 120-second idle and 240-second total
deadlines); root `check-qemu` took 28.7 s wall here, so the deadlines stand.
macOS keeps its skip row: Homebrew's qemu has no user-mode emulator, and the
new targets print `SKIP ... no ARM execution occurred` without `REQUIRE_QEMU`.
On a runner whose qemu-user lacks `cortex-m0`, `qemu-arm -cpu cortex-m0` exits
nonzero and the cpu-model gate fails loudly; the control rows then name the
cause.

Next steps, in value order:

1. Move `tests/libbn`, `tests/libcalc` and `usr.bin/calc` `check-qemu` to
   `${QEMU_ARM_RUN}` (verified green on `QEMU_ARM="qemu-arm -cpu cortex-m0"`;
   the commands run were `bmake -C tests/libbn check-qemu`, and the libcalc
   and calc equivalents).
2. Add rank 1 (`tests/kernel` ILP32 programs) and rank 3 (`tests/rp2040`
   USB, Dhara and flash gates) in that order, each with a mutant from its host
   gate.
3. If the owner wants the a.out loader as a gate, add `getdtablesize`, `stat`,
   a root-prefix option and the fork/exec calls first, then wire a
   `check-qemu-aout` target that depends on the built `lib/libc.a`, as
   `usr.bin/smlrc` already does.
4. Run the whole-kernel experiment above.

## Not run

| item | reason |
| --- | --- |
| build or boot of the `raspi-pico` fork | a QEMU build is outside a read-only research task and this host had 240 MB free in `/tmp`; the machine facts above come from the cover letter and `raspi-pico.rst` |
| reading the patches themselves | only the cover letter, the documentation file and one review reply were read; claims about MPU and ROM fidelity are the author's |
| `bmake logged-check` and every other tier | only `check-qemu`, the new targets, `check-lint`, `check-text-hygiene`, `check-comment-hygiene` and `check-test-execution` ran |
| older QEMU releases (Ubuntu runners) | only 11.1.3 was available; `-cpu cortex-m0` in user mode and the SIGBUS/SIGILL behavior need a `qemu-arm --version` row in the CI log |
| macOS, clang | not available |
| a loader mutation run (a libc `svc` change) | no such change was made; the falsifier in Question 2 is stated, not measured |
| `streamtest`, `sigtest`, and any program using fork, signals, tty or directories under the loader | calls not served |
| TCG plugin syscall filtering | headers not inspected |
| the USB, Dhara and flash gates (rank 3), `tests/kernel` (rank 1), the picocalc console test and the utility contract gates | ranked from the Makefile and source shape, not built for ARM |
