# Tests and gates

Every test in the tree is reached through a root Makefile target, and the
targets are grouped into tiers by what the host needs. `bmake
MACHINE=rp2040 check` runs every tier; a host without one tool runs the
tiers it has and names the one it skips. The cross, qemu, mips and
board-build tiers run after `bmake MACHINE=rp2040 build`, which leaves
the kernels, the board libc and the distribution tree the gates read.

| tier | target | needs | Linux CI | macOS CI |
| --- | --- | --- | --- | --- |
| lint | `check-lint` | shellcheck, ruff | yes | yes |
| host | `check-host` | host cc, `${PYTHON}` | yes | yes |
| ILP32 host | `check-kernel-ilp32` | a host cc that can build 32-bit binaries | yes, in the 32-bit job | no: clang on Apple silicon builds no 32-bit target |
| shell conformance | `check-posix-sh` | Linux x86-64 with 32-bit libraries | yes | no: bin/sh keeps pointers in int and builds as a 32-bit binary |
| cross | `check-cross` | arm-none-eabi toolchain, capstone and pyelftools under `${PYTHON}`, a built tree | yes | yes |
| qemu | `check-qemu` | qemu-arm (qemu-user) | yes | no: Homebrew's qemu builds no user-mode emulator; the Smaller C suite links only and says so |
| renode | `check-renode` | Renode, the fetched RP2040 models, a built tree | no: the models are a git clone and a dotnet build | no |
| mips | `check-mips` | a bare-metal MIPS cross compiler (`MIPS_GCCPREFIX`, mipsel-elf) | no: Ubuntu's mipsel-linux-gnu binutils know only elf32-tradlittlemips, not the elf32-littlemips that lib/elf32-mips.ld names | no MIPS toolchain in Homebrew |
| host package | `check-host-package` | ruff, pytest | host.yml on Ubuntu, Windows and macOS | host.yml |
| board build | `check-board-build` | arm-none-eabi toolchain, a built tree | yes | yes |

`.github/workflows/firmware.yml` runs the tiers after the warning-free
build; `host.yml` owns the discobsd-host package and packages it on
three platforms. `PYTHON` defaults to `python3` in share/mk/sys.mk and
the root Makefile and reaches every sub-make.

Windows appears in `host.yml` and nowhere else. The firmware tiers want
bmake, an arm-none-eabi cross toolchain and a POSIX sh userland to test,
so the platform that runs them on a Windows machine is WSL, which is the
Ubuntu job already. What Windows does carry on its own is the
discobsd-host package: the pytest matrix runs there, and a separate job
builds the PyInstaller executables that talk to a board over USB.

## Lint

`tools/check-lint.sh` runs shellcheck at error severity over every
tracked `*.sh` file and every tracked file whose first line is a sh
shebang (share/man/makewhatis.sed carries one and is a sed script, so
it is excluded by name), then ruff over every tracked Python file.
`.shellcheckrc` declares sh for the board scripts that carry no shebang
(usr.bin/true, false, nohup, lorder, unixbench). `ruff.toml` at the top
of the tree selects E, F, W, B, UP and I at line length 100; the host
package's pyproject.toml carries the same rules for its own directory.

## Host tier

Each gate compiles the tree's own source for the host, with `-Wall
-Wextra -Werror`, around a model of what the board supplies.

| gate | proves |
| --- | --- |
| `check-aout` | sys/sys/exec_aout.h's midmag macros and the layout check exec runs before committing to an image |
| `check-kernel` | seven sys/kern sources compiled from the kernel tree and run against 977 assertions: subr_rmap.c, the swap allocator, in three descriptor shapes; kern_subr.c, the uio machinery under every read and write; tty_subr.c, the character lists every tty queues through; kern_prot.c, kern_prot2.c and kern_proc.c, the protection syscalls and the process lookups they decide with; kern_resource.c, scheduling priority, resource limits and usage accounting |
| `check-libc-environment` | setenv, unsetenv, putenv and getenv over a modeled environ |
| `check-libc-tempfiles` | tmpnam, tempnam and tmpfile, on the tree's and the host's libc |
| `check-id-aliases` | id, whoami, groups and logname over stubbed identity calls |
| `check-tiny-utility-multicall` | true, false and nohup dispatch, arguments, signals, priority, streams, terminal and exit status |
| `check-portable-utilities` | getopt, yes, strings and users, with write-error injection |
| `check-pdp11-reference` | the unit tests of the PDP-11 V7 reference runner (the simh run itself is `check-pdp11-v7` with `PDP11_V7_IMAGE`); the three tests that publish evidence use Linux renameat2 and skip elsewhere, saying so |
| `check-fgrep-capacity` | fgrep's allocation boundaries and its command path over regular, empty and fifo pattern sources |
| `check-config-makefile` | the config tool regenerates the tracked kernel Makefile byte for byte |
| `check-swapram-evac` | the compressed swap pool's evacuation to flash, linking the kernel's swapram.c and subr_rmap.c |
| usr.bin/pdp11 `test` | the host build of the emulator boots the V6 pack on a pseudo-terminal |
| usr.bin/stevie, kilo, menu `test` | each editor driven through a pty |
| usr.bin/tail, sort `test` | output modeled against the host for every option |
| games/keen, bubble, fifteen `test` | a seeded game played through a pty; keen's solution uniqueness against an independent counter |
| bin/sh/tests `test` | the line editor through a pipe |
| bin/tar/tests/tartest.sh | the header formats |
| usr.bin/textbox/tests/run.sh | the sbase text tools against GNU coreutils and sharutils |
| usr.bin/cpio/tests/cpiotest.sh | odc archives round-tripped through the host cpio |

`check-posix-sh` runs bin/sh/tests/posix-sh.sh, the conformance harness
against XCU chapter 2, which builds the shell as a 32-bit host binary;
a case the shell does not yet answer as POSIX does is declared `xfail`
and fails the moment the shell starts producing the POSIX answer.

### The kernel's printf, and why it needs a narrower host

`check-kernel-ilp32` is separate from `check-kernel` for one file.
sys/kern/subr_prf.c carries its own argument walk,

    #define va_arg(ap,type) *(type*) (void*) (ap++)

over a `u_int *`, and `printf()` hands it `&fmt + 1`, the address just past
its own first parameter. Both hold where a pointer, a long and an int are
four bytes and arguments sit on the stack. That is the target; it is also
what `cc -m32` produces on an x86-64 host. At the host's natural width a
`%s` would read four bytes of an eight-byte pointer and every argument after
it would be wrong.

The binary this builds is ordinary 32-bit userspace. It is not a container,
a virtual machine or a second operating system, and it needs none: what the
gate wants from the platform is the width of a pointer, and an x86-64 kernel
runs an i386 binary directly. That is the same reason bin/sh's conformance
tier builds 32-bit, and the two share a CI job because they share the
`gcc-multilib` requirement.

Two paths that sound equivalent are not. GitHub's own runner ships
`linux-arm` (32-bit Arm) and no `linux-386`, so a genuinely 32-bit x86
worker would have to be driven from a supported controller or run a
different agent altogether; and a 32-bit Arm worker would give ILP32 at the
cost of hardware to maintain, for a property `-m32` already provides
exactly. Neither buys fidelity this gate lacks.

### Kernel sources on the host

`check-kernel` compiles a file from sys/kern unchanged and links it
against the harness in tests/kernel, so the gate measures the code the
board runs instead of a second copy of its algorithm. `check-swapram-evac`
links kernel sources the same way. Three properties of the tree decide the
shape of the harness, and a new gate that ignores any of them fails in a
way that looks like a bug in the kernel:

- sys/sys/types.h reads `typedef u_int size_t`, so a kernel source sees a
  32-bit size_t where a host source sees a wider one. hostkern.h therefore
  includes no system header and names no type the kernel names, and
  hostkern_kern.c is the only translation unit that includes both sides.
  A gate that reached `<stddef.h>` before `<sys/param.h>` would hand
  `malloc3()` an array of one width and get two of its three addresses
  written into the first element.
- A host binary links libc, which owns `printf`, `malloc` and `mfree`. The
  Makefile moves those names aside for the kernel source; hostkern_kern.c
  compiles with the same renames and includes both headers, so a signature
  that drifts from sys/systm.h becomes a conflicting declaration.
- sys/param.h reaches the port's headers as `<machine/*.h>`, a name
  config(8) makes in the kernel build directory. This tier has to run in an
  unconfigured tree, so its Makefile generates one forwarding header per
  port header instead.
- The port's interrupt primitives are ARM inline assembly no host assembler
  accepts. The generated `<machine/intr.h>` includes the port's header for
  its constants and then hostintr.h, which redefines the six spl macros; the
  port's own inline functions survive as static inlines nothing calls, so no
  assembly is emitted. The stand-in counts the level rather than discarding
  it, which turns "this routine lowered priority again" into something a
  gate states rather than a property of the shim.

A kernel source compiles here under `-Wall -Werror`, the set
sys/arch/rp2040/conf builds the kernel with, while the gate's own sources
take `-Wall -Wextra -Werror`. A gate that rejected code the production
build accepts would fail for something that ships, which is also why the
tier carries two suppressions that apply to no other tree: clang rejects
sys/kern's old-style function definitions, which the arm-none-eabi gcc the
kernel is built with accepts, and it checks the operand widths of the
port's PRIMASK inline assembly even where hostintr.h has redefined every
macro that would call it.

`panic()` returns control to the harness inside `HK_EXPECT_PANIC`, which
is what lets a gate assert on the defensive checks rather than only on the
paths that return. Outside one it prints and exits, so an unexpected panic
fails the gate rather than unwinding into unrelated code.

## Cross tier

| gate | proves |
| --- | --- |
| `check-divider` | neither linked kernel reaches the SIO divider registers, from the ELF and from the dependency files |
| `check-swapram` | vm_swap.o, exec_hsaout.o and kern_sysctl.o agree with each kernel's Config on the SwapRAM tier and pool size |
| `check-cache-footprint` | the name, buffer and inode caches' ABI and chain invariants, the exec argument spool over modeled SwapRAM, raw swap and buffers (including the swap cursor's rotation, publication and wrap), and the evacuation model |
| `check-exec-spool` | sys/kern/exec_subr.c spooling arguments through the real sys/kern/subr_rmap.c reservation, over modeled SwapRAM and buffers |
| `check-ufs-prototypes` | every UFS function the kernel links has a prototype |
| `check-hsaout` | the packed a.out container against header and stream corruption, truncation and forged lengths, over every image in the distribution tree |
| `check-libc-contracts` | raise and ctermid on the host, and the board libc's a.out contracts after a rebuild from clean |
| `check-flash-swap` | the raw flash swap driver's arithmetic on the host and the kernels' link map |
| usr.bin/as/tests `test` | the a.out assembler, archiver and linker: Thumb encodings against GNU as, archive names and rewrites, a linked program |
| tests/rp2040/divider_ownership | the divider verifier's positive and negative fixtures |

## qemu tier

This tier runs **user-mode** qemu, which is not an emulated machine.
qemu-arm loads one Arm program and translates its instructions on the host
processor, turning each Linux system call the program makes into the host
kernel's. It never boots this kernel and cannot: the guest's kernel is the
one already running the runner. What the tier therefore proves is that a
piece of Arm code computes what it should, which is exactly the right tool
for an instruction sequence and the wrong tool for a system call.

Full-system emulation is the other thing, and for this port it means
Renode: a modelled RP2040 that resets into the real boot ROM and runs this
kernel. `check-renode` uses it. See the Renode tier below for how far it
gets and why.

tests/rp2040/uarea_exchange assembles the u-area exchange loop cut from
locore.S between its markers and runs it under qemu-arm, checking every
exchanged byte. usr.bin/smlrc's suite compiles each test with the
Thumb-1 back end, links it against a copy of the tree's libc with the
Boot ROM float members removed (qemu-arm maps no ROM at address 0x10,
so libgcc's soft-float stands in) and the Linux syscall layer in
tests/qemusys.c, and compares the output; `REQUIRE_QEMU=1` refuses the
link-only run the tier would otherwise silently accept.

## MIPS tier

tests/rp2040/elf2aout_layout links the same fixtures with the arm and
the MIPS toolchains and checks elf2aout's layout and the multicall bss
overlay on both. It needs a bare-metal toolchain (Arch's mipsel-elf,
the OpenBSD and FreeBSD prefixes in share/mk/mips-toolchain.mk): the
Linux-target mipsel-linux-gnu binutils reject the elf32-littlemips
output format the linker script names, so the tier runs locally and
not in CI.

## Renode tier

`check-renode` runs tools/renode/check-boot.sh, which boots the UART0
console kernel from the real RP2040 boot ROM and asserts the console
through the device probe. It stands outside `check` because it wants four
things this tree does not carry, and reports whichever is missing by name:
the `renode-test` harness, the models `tools/renode/fetch-renode-rp2040.sh`
clones, a PICO_UART kernel, and a flash image.

tools/renode/machine.resc builds the machine and stops there. boot.resc
includes it and adds a socket console and a GDB server for a person;
boot.robot includes the same file and attaches a terminal tester, so the
machine someone debugs is the machine the gate asserts on.

`renode-test` drives the suite through whatever `python3` resolves to, and
Renode's own harness needs robotframework and four other packages no
distribution installs with the emulator. check-boot.sh builds a virtual
environment from Renode's own `tests/requirements.txt` under
tools/renode/vendor, which .gitignore already excludes, rather than naming
versions here that would drift from the installed emulator.

One trap worth knowing: Robot Framework separates arguments on two or more
spaces, so a console line like `phys mem  = 264 kbytes` has to spell its
padding `${SPACE}${SPACE}`. Written plainly, the rest of the line is parsed
as the next argument and the failure reads as a type error inside Renode.

## Board tier

`check-board-build` builds the on-device regression programs to a.out
through tests/rp2040/Makefile: fptest (Boot ROM float, bit-exact),
sigtest (signal frames), streamtest (NSTATIC), tartest (the -Z filter
across a pipe), romprobe (ROM table dump), textcrc (packed executable
text restoration), epochtest, evactest, bigtest and hugetest (the
SwapRAM window epoch and evacuation). None is in the root manifest:
stage one with a `file /usr/bin/<name>` line in distrib/rp2040/mi.rp2040
for a test image and revert the line before committing. Three drivers
flash a board and check it over the USB console: board_aout_admission.py
(a truncated, an oversized and an even-entry image refused with distinct
errors), board_exec_hsaout.py (textcrc raw and packed across a swap) and
board_stack_align.py.

No tier runs the kernel under an emulator. Renode with the third-party
RP2040 models `tools/renode/fetch-renode-rp2040.sh` clones is the only
option that boots this kernel at all -- QEMU ships no rp2040 machine and
rp2040js models no flash writes -- and it does boot: through boot2, XIP,
clock bring-up, the real boot ROM's function table, Dhara, and on into
`execve`, and then it stops. `RP2040XIPSSI` shares its two FIFOs between
the CPU thread and its own clocking thread with no mutual exclusion and
loses bytes to the race, so the boot ROM ends up waiting under `splhigh()`
for data that no longer exists, which masks SysTick and stops the kernel
clock as well. Serializing the FIFOs clears that wedge and init then forks
a shell, but no run with or without the change has produced userland
console output, so no tier can assert on a prompt.

What every run does reach, in about two seconds of host time, is `swap size
= 380 kbytes`, and `check-renode` gates exactly that much. Reaching those
twelve lines exercises boot2, XIP entry, clock bring-up, the real boot ROM's
function table, the Dhara root and the device probe, and the sizes are
asserted rather than only the line names, so a flash layout change that
nothing else notices fails here.
sys/arch/rp2040/doc/research/emulation.md carries the measurements, the
diff, the replayable commands, and two further model defects.

Suites that run only on the board, because their program has no host
build or their reference output holds board addresses: usr.bin/cpp
`test`, usr.bin/picoc `test`, usr.bin/scm `tests` (closure names carry
heap addresses), usr.bin/retroforth/test, usr.bin/zmodem/ptest.sh, and
distrib/rp2040/tests (accept-div-printf.c under the board's smlrc,
accept-stdio-exec.c under the cross compiler; board-libc.md in
research/ names them).
