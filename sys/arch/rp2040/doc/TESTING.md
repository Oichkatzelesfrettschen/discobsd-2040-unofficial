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
| flash-id | `check-flash-id` | cmake and a Pico SDK | yes: firmware.yml fetches the SDK at a pinned commit | no: the job runs on Ubuntu alone |
| mips | `check-mips` | a bare-metal MIPS cross compiler (`MIPS_GCCPREFIX`, mipsel-elf) | no: Ubuntu's mipsel-linux-gnu binutils know only elf32-tradlittlemips, not the elf32-littlemips that lib/elf32-mips.ld names | no MIPS toolchain in Homebrew |
| host package | `check-host-package` | ruff, pytest | host.yml on Ubuntu, Windows and macOS | host.yml |
| board build | `check-board-build` | arm-none-eabi toolchain, a built tree | yes | yes |

The build runs in parallel. `bmake -j"$(getconf _NPROCESSORS_ONLN)"` takes
the job count from the machine; `getconf` is POSIX and answers on Linux and
macOS alike, where `nproc` is coreutils and absent from macOS. On a
12-thread host the kernel goes from 8.07 s to 1.55 s and the whole world
from 136.94 s to 31.10 s.

The tree is safe to build that way. Every kernel object is byte-identical
across a serial build and three parallel ones. Two parallel builds of the
world differ in 9 of 3736 artifacts and every one is a timestamp: seven
`.a` archives, whose per-member mtimes the tree's `ar` records with no
deterministic mode to suppress them, and `pdc`, whose y.tab.c prints
`__DATE__` and `__TIME__`. The 1286 library objects those archives wrap
are identical. A linked kernel is never byte-reproducible either way,
because conf/newvers.sh regenerates vers.c on every link.

The gates run serially. `check` and its tiers carry no `-j`, because a gate
may rebuild what another gate is reading: `tests/libc_contracts/Makefile`
runs `symlinks tools` in the top of the tree, and a sibling gate linking
against `tools/bin/config` at that moment is told the file cannot be made.
Making the tiers parallel-safe means giving each gate that rebuilds shared
state its own tree, which no gate does yet.

One failure under `-j` was not a race and is fixed: bmake advertises its
jobserver to children as `-j N -J fd,fd` in `MAKEFLAGS`, and the two
consumers in this tree that are not bmake both choke on it. GNU make, which
`check-swapram-evac` calls, rejects `-J` and prints its usage;
`tests/warning_policy/check.py` runs bmake through `subprocess`, which
closes the inherited descriptors, so the child reports `Invalid internal
option "-J"` onto the output the gate parses. Both clear `MAKEFLAGS` and
`MFLAGS` for the child.

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

## Compiler warning policy

The RP2040 config template and its tracked PICO/PICO_UART Makefiles use
`-Wall -Wextra -Werror`. The host kernel-source harness uses the same
warning groups, with its existing host-compiler compatibility exceptions.
The other ports' kernel templates still select their own warning groups.

`share/mk/warnings.mk` defines `WARNERR=-Werror`. `share/mk/sys.mk` and
`tools/Makefile.inc` put it on the compiler command rather than in
`CFLAGS`: several leaf Makefiles replace `CFLAGS`, including on a
command-line optimization override. Host-only `CC` replacements and host
build generators carry the same policy. This makes the warnings selected
by each legacy userland Makefile fatal; it does not claim that every
legacy source is clean under `-Wall -Wextra`.

`check-warning-policy-host` belongs to the host tier and
`check-warning-policy-cross` to the cross tier. They compile clean
controls and require a deliberately emitted warning to fail using the
evaluated commands of representative tools, shared target rules and
CFLAGS-replacing leaves. Each shared route is checked again with
`CFLAGS=-O0`. Both kernel configurations must reject separate `-Wall`
and `-Wextra` probes, and their warning assignments must agree with the
config template. A missing compiler or a broken clean control fails;
it is not mistaken for successful warning rejection. The tests compile
objects in temporary directories and neither link nor access a board.

The CI build-log assertion remains useful for diagnostics from generators
and linkers outside the C compiler's warning policy. It is not a substitute
for enabling warning groups. These gates do not prohibit deliberate
command-line replacement of `CC`, `CWARNFLAGS`, or `WARNERR`, or an
explicit `-Wno-*` override; such overrides are not a validated build.

Validate warning-policy changes from clean objects. The inherited userland
Makefiles do not track compiler flags as object dependencies:

```
bmake MACHINE=rp2040 cleanall
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 check-warning-policy-host check-warning-policy-cross
bmake MACHINE=rp2040 check-kernel check-config-makefile
```

## Host tier

Each gate compiles the tree's own source for the host, with `-Wall
-Wextra -Werror`, around a model of what the board supplies.

| gate | proves |
| --- | --- |
| `check-warning-policy-host` | enabled warnings are fatal through host tools and host-only overrides, even when CFLAGS is replaced |
| `check-build-failure` | a failed step cannot pass as success: lib/Makefile's install loop stops at the first failed child and its clean loop visits every child and keeps a failure; the kernel link recipe, lifted verbatim from the generated PICO Makefile, runs nothing after a failed newvers.sh, vers.c compile, size, objcopy, objdump or picotool, publishes no finished artifact from a failed step, tells an absent picotool from a failed one, and rejects an explicit unix.uf2 request without a working picotool. Every tool is a journaling stub that fails on request, and each negative case asserts the stub's own failure sentence, so the intended step is proven reached. The suite fails on the tree before the fix by behavior, not by a missing fixture |
| `check-aout` | sys/sys/exec_aout.h's midmag macros and the layout check exec runs before committing to an image |
| `check-fs-stress` | tools/fsutil, the host filesystem library every root image is built with: files across each indirection boundary, a free list fragmented by out-of-order deletes, a volume filled until it refuses, and the tree's own checker required to report nothing after each round |
| `check-kernel` | eight sys/kern sources compiled from the kernel tree and run against 1077 assertions: subr_rmap.c, the swap allocator, in three descriptor shapes; kern_subr.c, the uio machinery under every read and write; tty_subr.c, the character lists every tty queues through; kern_prot.c, kern_prot2.c and kern_proc.c, the protection syscalls and the process lookups they decide with; kern_resource.c, scheduling priority, resource limits and usage accounting; sys_generic.c, the read, write, readv and writev entry points, whose vector sum is held to SSIZE_MAX at the limit, beside it, for one oversized vector and for an overflow spread across vectors, against a 64-bit reference over 1100 vector sets, with a rejected request reaching no file operation and moving no offset, and the descriptor, count, copy, short-transfer and interrupted paths pinned. rwuio_setjmp.h maps the kernel's setjmp call onto the compiler's builtin so the file operation stub can longjmp out of it the way sleep() does |
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
| usr.bin/textbox/tests/run.sh | the sbase text tools against GNU coreutils and sharutils, and getline_test over the tree's own getline.c: buffer ownership after a refused growth, the byte that did not fit pushed back, the bytes before a stream error terminated, and the capacity policy held at SSIZE_MAX + 1 at the host width and at -m32 |
| usr.bin/cpio/tests/cpiotest.sh | odc archives round-tripped through the host cpio |

`check-posix-sh` runs bin/sh/tests/posix-sh.sh, the conformance harness
against XCU chapter 2, which builds the shell as a 32-bit host binary;
a case the shell does not yet answer as POSIX does is declared `xfail`
and fails the moment the shell starts producing the POSIX answer.

### The kernel's printf, and why it needs a narrower host

`check-kernel-ilp32` is separate from `check-kernel` for two files. The
second is sys/kern/sys_generic.c, built again as rwuio_test32: off_t,
size_t and u_int are all four bytes on the target, so the vector sum that
wrapped there is reproduced only at this width, and the gate asserts those
widths statically before it runs. The first is
sys/kern/subr_prf.c, which carries its own argument walk,

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

`check-warning-policy-cross` checks shared target warning enforcement and
both kernels' `-Wall -Wextra -Werror` behavior as described above.

| gate | proves |
| --- | --- |
| `check-divider` | neither linked kernel reaches the SIO divider registers, from the ELF and from the dependency files |
| `check-swapram` | vm_swap.o, exec_hsaout.o and kern_sysctl.o agree with each kernel's Config on the SwapRAM tier and pool size |
| `check-cache-footprint` | the name, buffer and inode caches' ABI and chain invariants, the exec argument spool over modeled SwapRAM, raw swap and buffers (including the swap cursor's rotation, publication and wrap), and the evacuation model |
| `check-exec-spool` | sys/kern/exec_subr.c spooling arguments through the real sys/kern/subr_rmap.c reservation, over modeled SwapRAM and buffers |
| `check-ufs-prototypes` | every UFS function the kernel links has a prototype |
| `check-hsaout` | the packed a.out container against header and stream corruption, truncation and forged lengths, over every image in the distribution tree |
| `check-libc-contracts` | raise and ctermid on the host, and the board libc's a.out contracts after a rebuild from clean |
| `check-libc-malloc` | lib/libc/gen/malloc.c, calloc.c and lib/libc/arm/sys/sbrk.c compiled from the tree over an sbrk() and _brk() the test owns, so exhaustion is a ceiling the test sets: a refused realloc() leaves the block allocated, shown by allocating again and requiring the result outside it; an oversized malloc(), calloc() or realloc() is ENOMEM before any arena call; calloc() rejects a product that wraps; growth into free neighbors and shrinking stay in place; a moved block frees its origin; 4000 mixed operations keep every live pattern; sbrk() returns -1 with brk's errno on refusal. The same binary runs at -m32 where the host can build it, which is the target's width. Against the allocator before the change the preservation phase fails and the oversized resize hangs, and sbrk.c before the change returns the old break on refusal |
| `check-libc-qsort` | lib/libc/gen/qsort.c compiled from the tree under another name: a sort of 48-byte records whose comparator sorts an array of ints before answering, which an implementation with file-scope record size and comparator finishes with the inner call's values; ordering against a reference at record sizes 1, 2, 4, 7 and 48 for counts from 0 to 300 around the insertion threshold; equal, sorted, reversed and three-key inputs; and size 0 or n under 2 as a no-op. Host width and -m32 |
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

## flash-id tier

`check-flash-id` runs tools/pico-sdk/check-flash-id.sh, which builds the
two SRAM-resident board probes, flash-id in tools/flash-id and
flash-semantics in tools/flash-semantics, against a Pico SDK and asserts
that each no_flash image still links and still fits SRAM. It stands
outside `check` for the reason the Renode tier does: it wants cmake and an
SDK this tree does not carry.

flash-id reads the flash chip's JEDEC identity and unique id.
flash-semantics measures what a model of that chip has to reproduce: the
status register's WEL after 0x06 and 0x04, WIP and its duration across a
sector erase and a page program, the erased state, that a program only
clears bits, page wrap at 256 bytes, and the JEDEC and SFDP answers. It
erases and programs one sector, the last 4 KB of the kernel region, which
the image does not reach, and leaves it erased. Neither probe is in the
root manifest and no gate runs either on the board:
tools/flash-semantics/run-on-board.sh reboots the attached Pico into
BOOTSEL, executes the probe from SRAM, reads its report over USB CDC and
waits for the resident kernel's console to return, and it runs only when
a person invokes it. Its report for the board on hand is recorded in the
notes repository's DEVICE.md and cited by
docs/research/renode-rp2040-upstream-reports.md.

tools/pico-sdk/sdk-path.sh resolves the SDK from `PICO_SDK_PATH`, then
tools/pico-sdk/vendor/pico-sdk, then a pico-sdk beside this tree in the
same workspace, then /usr/share/pico-sdk, /usr/local/share/pico-sdk and
/opt/pico-sdk, and names every candidate it checked when none is usable.
The packaged paths are named because a package that installs the SDK also
exports `PICO_SDK_PATH` from a profile snippet, and a profile snippet
reaches a login shell alone: a systemd unit, a cron job or a container
would otherwise be told to fetch an SDK the disk already holds.
It tests for `lib/tinyusb/src/tusb.h` rather than for the directory,
because a clone whose submodule is not initialized otherwise passes the
guard and fails inside CMake. tools/pico-sdk/fetch-pico-sdk.sh writes that
vendor copy, pinned by commit rather than by the 2.3.0 tag, and initializes
only lib/tinyusb.

The probe sources are tools/flash-id and tools/flash-semantics, or the
directories `PROBE_DIRS` lists; each CMake project is named after its
directory with `-` replaced by `_`. A missing SDK or a missing probe is
`not run` rather than a failure, so the gate reports a broken workspace instead
of a broken build. `SRAM_BYTES` and `STACK_MARGIN_BYTES` override the
budget so the size branch can be calibrated against an image known to be
too large.

tools/bench-flash-id-build.sh measures this route against two others and
uses the same resolver, so the benchmark and the gate cannot disagree about
which SDK they mean; docs/research/flash-id-build-routes.md carries the
numbers.

## Renode tier

`check-renode` runs tools/renode/check-boot.sh, which boots the UART0
console kernel from the real RP2040 boot ROM, asserts the console through
the device probe and on to a shell prompt, then holds the Renode log to a
fixed set of warning classes. It stands outside `check` because it wants
four
things this tree does not carry, and reports whichever is missing by name:
the `renode-test` harness, the models `tools/renode/fetch-renode-rp2040.sh`
clones, a PICO_UART kernel, and a flash image.

Nothing in this tree pins Renode itself, and the third-party models are
built against whichever one is installed, so the gate names both before it
runs anything: the emulator build it found and the commit the models are
pinned at. A build other than the one it was last verified against is a
note rather than a refusal, because a newer emulator is the ordinary case;
what matters is that a gate which starts failing after a package upgrade
says so instead of sending the reader into the kernel. It was last verified
against Renode 1.17.0+20260907gitf1dd1b4af.

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

Each test case opens a Renode log under tools/renode/vendor/results, and
check-boot.sh runs tools/renode/check-warnings.py over both when the suite
finishes. The models warn wherever they fall short of the silicon, and a
boot that reaches a shell still produces some thirty-six thousand of them,
so the gate cannot forbid warnings; tools/renode/warning-classes.txt names
every class the boot is known to produce, says what the model is missing
and what the kernel sees because of it, and caps how many times each may
appear. An unlisted class fails, and so does a listed one over its ceiling,
and any line the models log at error level fails on its own, because a
boot that reaches a shell produces none.
Three classes mark state the kernel reads and gets wrong -- RESETS is
absent so RESET_DONE reads back all-ones, XIP_CTRL:FLUSH is decoded with no
behavior, and SYSINFO:CHIP_ID answers from the SVD reset value, which is
why the banner reports manufacturer 0x000 where silicon reports 0x493. The
rest are pins and bits nothing reads back. Ceilings ratchet down as the
models improve.

What `check-renode` does not decide is SSI concurrency. `Create Terminal
Tester` pauses the emulation at every wait, which serializes the two CPU
threads enough that a race between them rarely fires: the suite passed
against a model whose BUSY flag latched and hung every free-running boot.
The measurement that decides it is a free-running `RunFor` with no tester
attached, recorded in emulation.md.

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

The board tier is the only one that reaches silicon. Renode with the
third-party RP2040 models `tools/renode/fetch-renode-rp2040.sh` clones is
the only emulator that boots this kernel at all -- QEMU ships no rp2040
machine and rp2040js models no flash writes -- and it now boots all the way
to a shell. Two corrections took it there. `uartprobe()` counted its units
from one, as the STM32 ports do, so `device uart0` resolved to index -1 and
the console tty never attached; `cnputc()` writes the data register
directly, so the kernel's own output looked correct throughout while no
process could open /dev/console. And `RP2040XIPSSI` stored its BUSY flag
across a transfer that two CPU threads could interrupt, so a stall left it
set and the boot ROM's `wait_ssi_ready` spun forever;
tools/renode/patches/0002-xip-ssi-derive-busy-from-transfer-state.patch
derives the flag instead and serializes the transfer. Free-running boots
went from 0 of 6 reaching getty to 6 of 6. The fetch script applies the
whole series under tools/renode/patches in order: the FIFO lock, the
derived flag, a wait that the SSI model had logged as an error, and the
W25QXX corrections that tools/flash-semantics measured against the part.
Each patch is `git format-patch` output, so it carries its own message and
`git am` applies it upstream unchanged.

`check-renode` gates the console from the first banner line to a logged-in
shell that answers `uname -sr`. Reaching the device-probe lines exercises
boot2, XIP entry, clock bring-up, the real boot ROM's function table, the
Dhara root and the device probe, and the sizes are asserted rather than
only the line names, so a flash layout change that nothing else notices
fails here. Reaching the shell adds exec of a compressed a.out through the
swap writer, the tty layer, fork and wait, and the filesystem under write
traffic. It remains an emulator result: the models are third-party, three
warning classes mark registers whose answers differ from the chip's, and a
board run is still what settles a claim about silicon.
sys/arch/rp2040/doc/research/emulation.md carries the measurements, the
diffs, the replayable commands and the model defects that remain.

Suites that run only on the board, because their program has no host
build or their reference output holds board addresses: usr.bin/cpp
`test`, usr.bin/picoc `test`, usr.bin/scm `tests` (closure names carry
heap addresses), usr.bin/retroforth/test, usr.bin/zmodem/ptest.sh, and
distrib/rp2040/tests (accept-div-printf.c under the board's smlrc,
accept-stdio-exec.c under the cross compiler; board-libc.md in
research/ names them).
