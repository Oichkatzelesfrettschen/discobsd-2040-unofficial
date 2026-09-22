# Tests and gates

The root Makefile groups default tests into tiers by what the host needs.
`bmake MACHINE=rp2040 check` invokes the maintained tiers; a host without one tool runs
the tiers it has and names the one it skips. The cross, qemu, and board-build
tiers run after `bmake MACHINE=rp2040 build`, which leaves
the kernels and distribution tree the gates read. `check-cross` builds the
reduced board libc when its source closure is newer than the archive.

A tier that compiles creates `include/machine` first. `share/mk/sys.mk`
compiles with `-nostdinc -I${TOPSRC}/include`, and `sys/sys/param.h` and
`sys/sys/types.h` reach `<machine/machparam.h>` and `<machine/types.h>`
through that symlink, which is generated rather than tracked: a fresh
checkout carries none and `clean` removes it. `check-host`, `check-cross`,
`check-qemu` and `check-board-build` depend on `symlinks`, so each runs on a
bare checkout. A gate named on its own, such as `check-libc-ansi`, still
wants `bmake MACHINE=rp2040 symlinks` ahead of it. `check-lint` and
`check-host-package` compile no C and `bin/sh/tests/posix-sh.sh` builds the
shell against the host's headers, so those two reach nothing under
`include`.

| tier | target | needs | Linux CI | macOS CI |
| --- | --- | --- | --- | --- |
| lint | `check-lint` | shellcheck, ruff | yes | yes |
| host | `check-host` | host cc, `${PYTHON}` | yes | yes |
| ILP32 host | `check-ilp32-execution` | a host cc and runtime that execute 32-bit binaries, native and ILP32 sanitizers, `${PYTHON}` | required in `firmware/posix-sh` | optional variants report SKIP on hosts without ILP32 |
| shell conformance | `check-posix-sh` | Linux x86-64 with 32-bit libraries | yes | no: bin/sh keeps pointers in int and builds as a 32-bit binary |
| cross | `check-cross` | arm-none-eabi toolchain, capstone and pyelftools under `${PYTHON}`, a built tree | yes | yes |
| qemu | `check-qemu` | qemu-arm (qemu-user) | yes | no: Homebrew's qemu builds no user-mode emulator; the Smaller C suite links only and says so |
| renode | `check-renode` | Renode, the fetched RP2040 models, a built tree | no: the models are a git clone and a dotnet build | no |
| flash-id | `check-flash-id` | cmake and a Pico SDK | yes: firmware.yml fetches the SDK at a pinned commit | no: the job runs on Ubuntu alone |
| host package | `check-host-package` | ruff, pytest | host.yml on Ubuntu, Windows and macOS | host.yml |
| board build | `check-board-build` | arm-none-eabi toolchain, a built tree | yes | yes |

The build and parallel-safe gate groups run with `bmake
-j"$(sh tools/online-cpus.sh)"`. The resolver reads the online count from
`getconf`, BSD `sysctl` or GNU `nproc`, in that order, and returns one when a
host exposes none. On a 12-thread host the kernel goes from 8.07 s to 1.55 s
and the whole world from 136.94 s to 31.10 s.

The tree is safe to build that way. Every kernel object is byte-identical
across a serial build and three parallel ones. Two parallel builds of the
world differ in 9 of 3736 artifacts and every one is a timestamp: seven
`.a` archives, whose per-member mtimes the tree's `ar` records with no
deterministic mode to suppress them, and `pdc`, whose y.tab.c prints
`__DATE__` and `__TIME__`. The 1286 library objects those archives wrap
are identical. A linked kernel is never byte-reproducible either way,
because conf/newvers.sh regenerates vers.c on every link.

The host tier gives every suite a target and an output directory. The libc
contracts run under one sub-make, so bmake sees shared formatter objects and
creates each object once. Keen gives the fixtures one target, each unchecked
size one target, and each checked size four ten-seed targets. Nineteen
uniqueness shards use private temporary files; Python orchestrates bounded C
processes while bmake owns the cross-shard concurrency. The board-build tier
gives each on-device program a target. These graphs accept the jobserver count
directly.

The complete 12-job maintained host tier takes 5.80 seconds on the measured
host. Tail's 2,016 subprocess cases remain serial inside their target because
the protocol shares ordered state; independent suites and Keen shards run
concurrently around it. The separately selected PDP-11/V6 test is outside the
maintained host tier.

The cross tier separates isolated contract directories from shared kernel and
assembler outputs. `.WAIT` orders the PICO, PICO_UART, SwapRAM, exec-spool and
flash-swap readers, then runs the assembler last because its link proof
rebuilds the full a.out libc. The a.out archive contract reads the production
archives and rejects absent or stale inputs; it no longer deletes and rebuilds
the global tools directory inside a gate. The workflow gives lint, host, cross,
qemu and board-build separate steps and deadlines, so a stalled contract names
its tier instead of occupying one opaque combined step. A 12-job warm cross
tier completes in 33.52 seconds and the nine-program board-build tier completes
in 0.23 seconds on the measured 12-thread x86-64 host.

The `-j` failures were descriptor-lifetime defects rather than data races.
bmake advertises its jobserver as `-j N -J fd,fd` in `MAKEFLAGS`. GNU make
does not implement `-J`, and Python or shell process boundaries may close the
advertised descriptors before a nested bmake starts. `check-swapram-evac`,
the warning-policy subprocesses, the assembler's detached libc builds, the
UFS prototype verifier and the elf2aout layout verifier therefore clear
`MAKEFLAGS` and `MFLAGS` only at those boundaries. Ordinary recursive bmake
recipes retain the live jobserver.

`.github/workflows/firmware.yml` runs the tiers after the warning-free
build; `host.yml` owns the discobsd-host package and packages it on
three platforms. The caller supplies `PYTHON`; CI resolves one interpreter,
writes its path to the job environment, and every sub-make inherits that
identity.

### Required execution and optional local variants

`firmware/posix-sh` owns `check-ilp32-execution`. The job provisions
gcc-multilib, runs the existing libc host aggregate, kernel ILP32 aggregate,
shell conformance, dd, umount, backgammon, libc sysctl, and textbox suites.
The textbox suite includes the separately compiled `getline_test32`.
The libc ANSI ILP32 variant links the tree's own stream table.

`tools/test-execution-inventory.json` enumerates executable variants, their
compiler width, capability condition, build prerequisites, recipe, recursive
make target, aliases, and owning job. `.PHONY` entries are declarations,
excluded from execution edges. The inventory covers conditional ILP32 tests
and their libc native, sanitizer, and negative-control companions. Other
host gates retain their documented owners. Smaller C's differential `fuzz`
target is an explicitly optional investigation requiring cross artifacts,
qemu-arm and a 32-bit reference compiler; its prerequisite `test` does not
execute the fuzz recipe.

The outcomes describe execution, separately from root-target reachability:

| outcome | meaning |
| --- | --- |
| PASS | The executable ran and returned the contract's expected status. Negative controls must return exactly their recorded rejection status. |
| FAIL | The executable ran and rejected the expected result. |
| SKIP | An explicitly optional local variant could not execute. |
| ERROR | Required execution could not be established, including a missing runtime, executable, or receipt. |

`tools/compiler-probe.sh` compiles width assertions and executes the output;
successful linking alone cannot admit a variant. The same probe runs with
the sanitizer flags for each conditional sanitizer. Local suite targets may
skip unavailable variants. `REQUIRE_ILP32=yes` makes their ILP32 skips fail;
the designated aggregate also sets `TEST_EXECUTION_REQUIRED=yes`, requiring
the native sanitizer companions. Capability failure never supplies a PASS.
Undefined-behavior sanitizer diagnostics terminate the required sanitized
executables through `-fno-sanitize-recover=all`.

The recorder surrounds each executable recipe with its actual command,
working directory, executable SHA-256, compiler-width class, expected and
observed exit statuses, and owner. Shell harness records name the script and
the built subject. The aggregate creates a fresh private receipt directory,
then compares observed receipts with the full inventory even when make
fails. A target that returns zero while omitting a required invocation
produces ERROR. These records establish execution of the reviewed harness;
the harness assertions and calibration establish the behavior exercised.
The detached make clears inherited jobserver flags and receives the caller's
command-line variable assignments explicitly, preserving compiler overrides
with arguments. Required execution flags and the private receipt directory
override caller values at that boundary.

Run the same required aggregate locally with a caller-selected interpreter:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-test-execution
bmake MACHINE=rp2040 check-ilp32-execution \
    TEST_EXECUTION_REPORT=/tmp/ilp32-execution.json
```

CI retains `ilp32-execution.json` as the `ilp32-execution` artifact, including
failed invocations. The report identifies the source revision, inventory
hash, tracked-diff hash and modification flag. Each executable record also
identifies the tested binary by hash. Compile failures leave an ERROR row
for each unexecuted required variant; the build log carries the diagnostic.
Receipt and probe directories are private to each invocation. Compiled
suite products belong to the checkout, so independent aggregates use
separate worktrees; one aggregate may schedule its prerequisite graph in
parallel, as the existing host tier does.

`check-test-execution` calibrates successful and rejecting processes, exact
negative-control status, optional and required capability branches, missing
executables, width mismatch, duplicate receipts, missing receipts, a
successful aggregate that executes nothing, compiler output that cannot
run, command-line override forwarding, the stdio header probe's compiler
argument boundary, and the real backgammon Makefile's optional/required split. Host
execution proves the exercised host conditions. Cross, emulator and board
results retain their separate meanings.

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

`share/mk/warnings.mk` defines `WARNERR` from `WARNLEVEL`: the default
level, `full`, is `-Wall -Wextra -Werror`, and `legacy` is `-Werror`
alone, so only the groups a Makefile's own CFLAGS name are fatal there.
A leaf Makefile assigns `WARNLEVEL=legacy` before it includes sys.mk,
because sys.mk composes `CC` at include time and a later assignment
selects nothing; the two comment lines above the assignment state the
number of distinct sites `tools/warning-census.sh` measured in that
directory, so `git grep WARNLEVEL` is the ledger of what remains open.
`share/mk/sys.mk` and `tools/Makefile.inc` put `WARNERR` on the compiler
command rather than in `CFLAGS`: several leaf Makefiles replace
`CFLAGS`, including on a command-line optimization override. Host-only
`CC` replacements and host build generators carry the same policy.
`docs/research/warning-census.md` records the measurement behind the
split: 100 of the 241 leaf directories build at the full level and 141
at legacy.

`check-warning-policy-host` belongs to the host tier and
`check-warning-policy-cross` to the cross tier. They compile clean
controls and require a deliberately emitted warning to fail using the
evaluated commands of representative tools, shared target rules and
CFLAGS-replacing leaves. Each shared route is checked again with
`CFLAGS=-O0`. The full-level cross routes, bin/echo, usr.bin/smlrc and
lib/libc, must reject separate `-Wall` and `-Wextra` probes; the legacy
routes, bin/sh and usr.bin/uucp, must reject the fatal probe and
compile the `-Wextra` probe to an object, which proves the declaration
reached `CC`. The host tier reads every tracked Makefile and fails a
`WARNLEVEL` assignment that follows its sys.mk include or names a level
warnings.mk does not accept. Both kernel configurations must reject
separate `-Wall` and `-Wextra` probes, and their warning assignments
must agree with the config template. The cross tier also compiles each route with the
override `tools/warning-census.sh` uses, `WARNERR=-Wall -Wextra
-Wno-error`, and requires the `-Wall` and `-Wextra` probes to raise their
diagnostics and still produce an object: that is what makes the census a
measurement of every compile the policy governs, including the
CFLAGS-replacing leaves, rather than of the compiles COPTS happens to
reach. A missing compiler or a broken clean control fails;
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
| `check-agent-instructions` | The repository has regular canonical and wrapper files; the wrapper is exactly `@AGENTS.md` plus a newline and the canonical policy has literal references instead of active imports. Calibration exercises malformed, missing, cyclic, escaping and private-home imports, modes, staged/unstaged inversions and Git infrastructure errors. The gate verifies the chosen two-file graph, not actual client instruction loading. |
| `check-config-generated-sync` | A generator built from the tested host sources regenerates PICO and PICO_UART Makefiles byte for byte in private trees. The gate retains production board identities and compares comments, barriers, options, source lists and flags without normalization. Mutation controls reject stale template/Config/file-list/output combinations, missing inputs or outputs and generator failure; synchronized edits, unrelated source edits and concurrent isolated generation pass. |
| `check-config-include-order` | The actual template and production `SYSTEM_DEP` declarations and include-link recipes hold include creation behind configuration, and host compilation behind include creation. A controller holds each prerequisite group until the expected event; removing either barrier must produce its specific premature-execution verdict. Deadline expiration is an infrastructure error. |
| `check-warning-policy-host` | enabled warnings are fatal through host tools and host-only overrides, even when CFLAGS is replaced; every WARNLEVEL assignment precedes its sys.mk include and names a level warnings.mk accepts |
| `check-build-failure` | a failed step cannot pass as success: lib/Makefile's all target enters every subdirectory even when the directory's mtime is not older than the make, which bmake otherwise reads as up to date against FRC and skips (the case dates the directories an hour ahead; before the subdirectory targets were phony a clean in the same second as the build left lib/startup-arm unentered and lib/crt0.o missing); its install loop stops at the first failed child and its clean loop visits every child and keeps a failure; the kernel link recipe, lifted verbatim from the generated PICO Makefile, runs nothing after a failed newvers.sh, vers.c compile, size, objcopy, objdump or picotool, publishes no finished artifact from a failed step, tells an absent picotool from a failed one, and rejects an explicit unix.uf2 request without a working picotool. Every tool is a journaling stub that fails on request, and each negative case asserts the stub's own failure sentence, so the intended step is proven reached. The suite fails on the tree before the fix by behavior, not by a missing fixture |
| `check-analysis` | The 2.11BSD mapper pins donor and recipient commits, retains relocated and absent paths, preserves NUL-delimited filename bytes, inventories merge parents, and distinguishes Git failure from absent entries. Calibration shows child-like defective text and parent-like equivalent text remain textual candidates, with semantic dispositions outside the tool. Dirty files and refs advanced after resolution leave the pinned comparison unchanged. The separate semantic-ledger verifier authenticates the retained tested-integration commit object against the landed tree, reads source and inventory blobs through both identities, derives receipt expectations from those pinned bytes, and checks selected row membership, owner/width/invocation joins, dependencies, open next actions and its Markdown projection. Those checks establish ledger integrity rather than semantic truth or whole-series coverage. |
| `check-aout` | sys/sys/exec_aout.h's midmag macros and the layout check exec runs before committing to an image |
| `check-fs-stress` | tools/fsutil, the host filesystem library every root image is built with: files across each indirection boundary, a free list fragmented by out-of-order deletes, a volume filled until it refuses, and the tree's own checker required to report nothing after each round |
| `check-kernel` | eight sys/kern sources compiled from the kernel tree and run against 1194 assertions: subr_rmap.c, the swap allocator, in three descriptor shapes; kern_subr.c, the uio machinery under every read and write; tty_subr.c, the character lists every tty queues through; kern_prot.c, kern_prot2.c and kern_proc.c, the protection syscalls and the process lookups they decide with; kern_resource.c, scheduling priority, resource limits and usage accounting; sys_generic.c, the read, write, readv and writev entry points, whose vector sum is held to SSIZE_MAX at the limit, beside it, for one oversized vector and for an overflow spread across vectors, against a 64-bit reference over 1100 vector sets in each direction, with a rejected readv or writev reaching no file operation and leaving a nonzero offset where it stood, the sixteen-vector boundary summed through its last element, and the descriptor, count, copy, short-transfer and interrupted paths pinned. rwuio_setjmp.h resolves the kernel's setjmp call to the host library's over a jmp_buf the harness owns, so the file operation stub can longjmp out of it the way sleep() does |
| `check-kernel-metadata` | the syscall and errno masters regenerate every shipped output byte for byte and reject duplicate or missing ordinals, out-of-range argument counts, invalid conditions, orphaned or accidental public aliases, oversized 16-bit pools and incomplete errno sequences; the contract checker pins syscall 23, the vfork alias and dispatch slot, compact device and tty tables, every capacity allocation seam, process and clist occupancy formulas, historical notices, every generated RP2040/STM32/PIC32 Makefile input, and both reset-time u-area paints before `SystemInit`. Known-bad metadata, device, tty and reset-paint fixtures calibrate the verdict. The gate reads and generates temporary files; it neither boots a kernel nor establishes workload headroom |
| `check-root-noatime` | both RP2040 configurations and their generated Makefiles select `ROOT_MOUNT_FLAGS=MNT_NOATIME`, the initial root mount consumes the policy, `/etc/fstab` preserves it across `mount -a`, automatic read sites retain their guards, and explicit atime requests remain outside those guards. Five in-memory mutations calibrate the policy checker |
| `check-libc-environment` | setenv, unsetenv, putenv and getenv over a modeled environ |
| `check-libc-sysctl` | `uname` and `gethostname` compiled from the tree against a `sysctl()` that answers as sys/kern/kern_sysctl.c's helpers do: the prefix that fits, the length the value needs, ENOMEM. Every `utsname` field ends terminated and the one walk over a field's contents takes its bound from the field, which a guard of the byte that walk rewrites and an exactly sized allocation under the sanitizers both hold it to; `gethostname` terminates a truncated name and reports ENAMETOOLONG. The tree's userland `size_t` is `u_int`, so the tier runs at ILP32, and the gate reaches the tree's headers while its reporting half reaches the host's. Against the pre-change sources 7 of 31 checks fail and AddressSanitizer names a heap-buffer-overflow read in `uname` |
| `check-libc-tempfiles` | tmpnam, tempnam and tmpfile, on the tree's and the host's libc |
| `check-stdio-bounds`, `check-stdio-bounds-cross` | `gets(3)` is absent from the tree in the two places that make a caller impossible: `<stdio.h>` does not declare it, so a call fails at its implicit declaration, and neither `lib/libc/stdio/Makefile` nor `lib/libc_aout/libc/Makefile` builds an object, so a caller that declares it itself fails at the link. The cross form reads the built `lib/libc.a` for the symbol as well, which also catches an incremental tree where `ar` still holds a `gets.o` no rule rebuilds. `fgets` is the control at every step, so an unusable include path or an empty archive fails the gate rather than passing it. Restoring the declaration, either Makefile entry, or a rebuilt archive containing the object each fails one check. No source sweep: the two facts above leave nothing for one to add beyond a heuristic that cannot tell a call from a line of comment |
| `check-libc-zone` | the zone file reader, which is the one part of libc that decodes bytes a caller names through TZ. Runs in both shapes it is built in: the file-backed one, where every count in the file bounds a walk over a fixed-size array, and the default one, which asserts a zone file named in TZ is not opened and the answer is the kernel's single offset. Host width, ILP32, and under the address and undefined sanitizers. A build that reached a file anyway does not compile, tzload() being undeclared there |
| `check-libc-ctime`, `check-libc-ctime-cross` | the exact fixed-width formatter and UTC decomposition compiled as C17 at host, ILP32 and Cortex-M0+ widths: canonical output, one-digit day padding, four-digit year and leap-second boundaries, null input, both sides of every field range, unchanged static output after rejection, epoch and pre-epoch decomposition, leap day, both signed 32-bit time boundaries, an exact freestanding production compile that accepts libc's `%D` extension, a supplemental builtin-remapped target overflow compile and calibrated negative formatter, and a behavioral control that accepts invalid fields. GCC reserves builtin format and object-size analysis for the explicit diagnostic lane because its printf grammar omits the shipped `%D` conversion |
| `check-libc-random`, `check-libc-random-cross` | random, srandom, initstate and setstate compiled as strict C17 at host, under the undefined-behavior sanitizer, at ILP32 and Cortex-M0+ widths: an independent 32-bit reference checks four seeds across every state-size boundary; state switching, canaries, alignment, undersized buffers and malformed metadata pin commit-before-use behavior. A fixture that installs malformed metadata calibrates rejection. The target object must remain allocation-free and import neither division helpers nor stdio |
| `check-libc-runtime-limits`, `check-libc-runtime-limits-cross` | the five fixed DiscoBSD `sysconf` selectors and `_CS_PATH` at host, ILP32 and Cortex-M0+ widths: exact values, size queries, every truncated length, null output, unchanged output after an unknown selector, and `EINVAL`. A permissive-selector fixture calibrates rejection. Separate target objects must use constant storage and import only `errno`; the a.out archive gate proves both APIs remain available to on-device links |
| `check-libc-difftime`, `check-libc-difftime-cross` | `difftime` at host and ILP32 widths over zero, both directions, and the complete signed 32-bit endpoint span. A subtract-before-convert fixture calibrates the overflow case. The Cortex-M0+ object must import exactly `__aeabi_i2d` and `__aeabi_dsub`, proving endpoint conversion precedes the ROM-backed double subtraction. Board `fptest` separately gives `__aeabi_i2d` a bit-exact cold and cached corpus; build success does not substitute for that board run |
| `check-colrm-contracts`, `check-colrm-contracts-cross` | the exact colrm source at host and Cortex-M0+ widths: strict decimal bounds, inclusive removal, open-ended and single-column ranges, tabs, backspaces, newline reset, unterminated input, null and high bytes, column overflow, read failure, immediate write failure and final-flush failure. Exclusive-stop and ignored-flush source mutations calibrate the behavioral gate; the target object must avoid compiler division helpers |
| `check-unifdef-contracts`, `check-unifdef-contracts-cross` | the off-manifest unifdef source at host, under the address and undefined-behavior sanitizers, and at Cortex-M0+ width: classic `-D`, `-U`, `-iD`, `-iU`, `-l`, `-t` and `-c` parsing; exact statuses 0 unchanged, 1 transformed and 2 error, including byte-identical and comment-fragment complements; known, unknown and repeated-symbol conditionals; ignored-arm lexical suppression; single-line and multiline comments between directive tokens and comments that close before a directive; complete and split-delimiter trailing comments on removed directives; phase-2 splices in line comments, block-comment delimiters, keywords and symbols; vertical-tab and form-feed preprocessing whitespace; strings, null bytes and an unterminated final line; the 100-symbol, 64-frame and 4096-byte-logical-line bounds; deliberate `#elif` rejection; structural, read, write and final-flush failures. Permissive-`#elif` and ignored-flush source mutations calibrate the behavioral gate; the target object must avoid compiler division helpers. The executable remains absent from `distrib/rp2040/mi.rp2040` while its measured admission cost is evaluated |
| `check-libc-printf` | snprintf and vsnprintf compiled from the tree at host and ILP32 widths: required-length returns, exact fit, truncation termination, size one, `(NULL, 0)` measurement, destination guards, and rejection of a size that cannot fit the formatter's signed count; the length modifiers `hh`, `h`, `ll`, `z`, `t` and `j`, `%n` storing the count at each width, a negative `*` precision taken as omitted, the space flag, and the `%D` extension. The formatter before those modifiers fails five checks. The pre-change ILP32 binary fails three checks before its zero-size null write faults; the corrected sources pass every check at both widths |
| `check-libc-scanf` | `_doscan`, its float member, target `strtod` and target character-class table compiled from the tree at host width, at ILP32 and under the address sanitizer, over the `_IOSTRG` stream `sscanf` builds: every integer base and length modifier; saturation at signed and unsigned destination limits; `%i` prefix detection; C17 `%X` plus historical `%D` and `%O`; finite, overflowing and underflowing decimal exponents; scansets with ranges, negation, a leading bracket and no carry between directives; partial and complete `%c` fields without white-space skipping; `%n` excluded from the count; suppression; literal matching; field widths; and the EOF against matching-failure verdicts. The malformed group feeds a 512-digit run to an unwidthed `%ld` and `%u`. The gate is calibrated against the scanner it replaced: that build aborts under the sanitizer, first on `%X` storing a long through a pointer to `unsigned int`, then on the digit run overrunning a 64-byte staging buffer inside `_innum`'s frame. A further case pushes a byte differing from the one read onto a string stream over a literal: the V7 `ungetc` stored it into the literal and faults, the one-byte `_ub` slot in `FILE` takes it and hands it back first |
| `check-libc-rwmode` | `_filbuf` and `_flsbuf` compiled from the tree over a host temporary opened read-write: output, `fflush`, input continuing at the output position, input to end of file, output appending there, then the file read back whole. The V7 core fails both switches: after `fflush` the write buffer's free count let `getc` read the output buffer as input, and `putc` at end of file wrote the consumed read-ahead back as output |
| `check-libc-syslog` | syslog compiled as strict C17 over deterministic clock, errno and transport shims: priority, timestamp, tag, maximum target PID, CRLF, LOG_PERROR, repeated and escaped `%m`, long tag, literal format, argument and error-text bounds, and retry after a failed logfile open |
| `check-libc-vis` | the legacy and bounded visual encoders compiled as strict C17 with signed input bytes: all 65,536 byte, flag and octal-lookahead combinations preserve the historical encoding; focused cases pin exact and short capacities, destination guards, an unchanged destination on ENOSPC, embedded NUL input and high-bit bytes |
| `check-getty-contracts` | getty's public capability parser, flag derivation and exact mode-application function from `main.c`, compiled with its tables, virtual gettytab text and an ordered ioctl trace: the header indexes agree with the table; parsed omitted and explicit `rw` and `ec` select `RAW` and echo or `CBREAK` and disabled echo; parsed `np` delivers `LPASS8` after the low mode through ordered `TIOCSETP` and `TIOCLSET` calls; parsed `ep` or `op` replaces the shipped parsed `ap` default; parsed tab, flow-control and explicit `f0` choices retain their meanings. Fixtures that drop local-mode application or report every parsed flag absent must fail the same gate. The login half records `TIOCLGET`, a temporary zero-valued `TIOCLSET`, and restoration of the exact inherited local-mode word. An exact-source order check requires login to clear before authentication commits, restore before environment setup and execute the shell last; missing and prematurely restored fixtures calibrate that check |
| `check-libc-string-security` | explicit_bzero and timingsafe_bcmp compiled from the tree: zero, partial and complete erasure; zero length, equality and differences in the first, middle and final byte across all 256 byte values |
| `check-dirent-contracts` | the five directory-stream members compiled as strict C17 over deterministic open, close, read, seek and allocation shims: initialization, allocation-failure errno preservation, free-entry skipping, allocation-free cookies, same-buffer and cross-buffer seeks, mutation-visible rewind, seek failure, malformed record lengths and names, read failure and close ownership. The pre-change sources fail the gate at their K&R definitions before the layout fixture rejects the 1,108-byte stream |
| `check-cat-contracts` | cat's fixed-block raw path over read, write, descriptor and diagnostic shims: empty input, target-size reads, complete short writes, read and write failures, zero writes and invalid descriptors; a host executable also pins raw multi-file copying, every historical display option and self-output refusal |
| `check-dd-contracts` | dd's operand arithmetic, its behavior after a failed read, and where it truncates. Built at ILP32, because `off_t` is `long` and `long` is four bytes on the target: a 64-bit host would refuse the operands for being large rather than for wrapping and would measure nothing. A C harness renames `open`, `read`, `lseek` and `exit` and asserts the exact call sequence, since no ordinary file fails a read on demand; the synthetic input is bounded, so a copy that stops advancing past the failed block fails by name in under a second rather than looping. A filesystem run covers `seek=` preserving what it steps over and `conv=notrunc`. 40 checks. Three inverted sources calibrate it: dropping the checked arithmetic fails 8, dropping the error-path `lseek` alone fails 9, dropping the `O_TRUNC` removal and the `ftruncate` fails 4. Reverting the `lseek` alone fails harder than reverting both read-error hunks together, which is independent evidence that the two are one repair. The conversion tables are held to the three dd used to carry: `atoe` and `etoa` are bijections and exact inverses over all 256 bytes, and the four-exception form of `atoibm` reproduces the 256-byte table it replaced entry by entry, which the suite keeps verbatim as its reference. `atoibm` is asserted *not* to be a bijection, since 91 and 213 both reach 173 and 93 and 229 both reach 189, so no inverse of it exists. End to end, `conv=ascii` after `conv=ebcdic` returns every one of the 256 bytes through the real program, `conv=ibm` differs from `conv=ebcdic` at exactly inputs 33, 91, 93 and 124, and a short `cbs=` block pads with the EBCDIC space the table gives for 0x20. Dropping one exception fails 3 of 1290 table checks and 2 of 20 end-to-end; making the fall-through read `etoa` fails 232 |
| `check-rmdir-contracts` | rmdir's component-wise `-p` behavior over a temporary host filesystem: complete and trailing-slash chains, partial removal, preserved initial failure, continued multiple operands, `--` and usage; an exact-source syscall shim pins the top-level root stop before an empty pathname |
| `check-tee-contracts` | tee's exact source over injected reads, writes, opens, closes and signals: interrupted reads, short writes, zero writes, isolated output failure, descriptor capacity and failure status; a filesystem run pins append, truncation, grouped options, `--` and literal `-` operands |
| `check-du-contracts` | du's exact source on a host filesystem: ordinary, `-a` and `-s` output; more than 1,000 simultaneously live hard-link identities; incomplete link sets; defined overcounting after injected allocation exhaustion; multiple-operand failure, trailing-slash restoration and the 1,024-byte target path bound. The exact host object excludes `telldir` and `seekdir`, while a fixture that imports both calibrates the rejection path, because a location from one directory stream has unspecified meaning in a reopened stream |
| `check-resize-contracts` | the exact resize reply parser accepts only `ESC [ rows ; columns R` with values from 1 through 999 and no trailing byte; focused cases cover leading zeroes, signs, whitespace, missing fields, a missing `R`, extra separators and overflow |
| `check-find-contracts` | the locate database builders bigram and code from usr.bin/find over a generated pathname list: the pair stream, the coded database decoded back through an independent reader, the escape code for a count outside the byte range, the 256-byte header padded from a short bigram table, control-byte squelching, a final line ending at end of file without a newline, an empty list, a missing operand and an unreadable table. The record bound holds from both sides: a pathname of `MAXPATH-1` bytes is a record, and a longer one is refused by a message naming the bound rather than split into two records that name no file. Source assertions reject every call to `gets(3)`, and each run carries a watchdog so a read that stops consuming fails by the case's name. Mutations that drop the newline strip (7 of 37 checks) and that drop the bound report (8 of 37) calibrate the behavioral cases, while restoring `gets(3)` fails the tier at its implicit declaration |
| `check-backgammon-contracts` | the shipped `games/backgammon/subs.c` linked against stand-ins for its siblings, driving `readc()` from a pipe and `crterase()` over the erase characters a terminal is set to. `readc()` ends the game on `tchars.t_intrc` and on nothing else, so it hands `CERASE` back for `table.c` and `save.c` to compare, and a rebound interrupt character moves the exit with it; `crterase()` answers from `LCRTERA` in the local mode word, which the console drivers set and `stty(1)` and gettytab change, and from nothing else: the suite drives every erase character the game can be given against each setting of the flag, because which of backspace and DEL is bound to erase says nothing about what the display does with the byte written back. Each case is read from a child process, so a regression in the quit character reports rather than taking the run down with it. Calibrations that fail it: the fixed `'\177'` quit (4 of 33 checks) and the fixed `'\003'` quit that 2.11BSD patch 478 proposed (2 of 33); and, for the display class, reading the erase character in any of its three forms, reading `LCRTBS` in place of `LCRTERA`, or answering yes unconditionally (10 of 33 each) |
| `check-id-aliases` | id, whoami, groups and logname over stubbed identity calls |
| `check-tiny-utility-multicall` | true, false and nohup dispatch, arguments, signals, priority, streams, terminal and exit status |
| `check-portable-utilities` | getopt, yes, strings and users, with write-error injection |
| `check-architecture-isolation` | the two maintained ARM tuples, invalid registry inputs, all generated ARM kernel Makefiles, default and opt-in traversal, the 1,032-row relocation map, the exact retained portability-selector set, filesystem composition, atomic tuple publication, and cleanup failure visibility; deliberate bad fixtures calibrate every class |
| `check-legacy-pdp11-v7-runner` | with `BUILD_PDP11_V6=yes`, the unit tests of the isolated PDP-11 V7 reference runner; the three tests that publish evidence use Linux renameat2 and skip elsewhere, saying so |
| `check-legacy-pdp11-v7-reference` | with `BUILD_PDP11_V6=yes` and `PDP11_V7_IMAGE`, the external SIMH/V7 image, profile, transcript, and simulator identity; the result does not test the repository emulator |
| `check-fgrep-capacity` | fgrep's allocation boundaries and its command path over regular, empty and fifo pattern sources |
| `check-config-makefile` | the config tool regenerates the tracked kernel Makefile byte for byte |
| `check-swapram-evac` | the compressed swap pool's evacuation to flash, linking the kernel's swapram.c and subr_rmap.c |
| `check-fs-profiles` | every maintained profile composes against the maintained manifest with resolved parents, links, closures, and paths. Its default selftest rejects the two broken toolchain selections and three deleted-path cases. With `BUILD_PDP11_V6=yes`, the same target consumes the two legacy fragments and additionally rejects an emulator without its pack, a PDP-11 closure without its emulator path, and a V6 closure without its guest path. `sys/arch/rp2040/doc/PROFILES.md` is the authority |
| `check-legacy-pdp11-v6` | with `BUILD_PDP11_V6=yes`, the isolated host build of the emulator boots the V6 pack on a pseudo-terminal |
| usr.bin/stevie, kilo, menu `test` | each editor driven through a pty |
| usr.bin/tail, sort `test` | output modeled against the host for every option |
| games/keen, bubble, fifteen `test` | a seeded game played through a pty; keen's four fixtures, 80 unchecked generator controls and 160 checked puzzles against an independent C17 row-permutation counter. Keen splits fixtures, unchecked cases and each checked size into bounded targets with private temporary files, so the jobserver can run the finite proof concurrently |
| bin/sh/tests `test` | the line editor through a pipe |
| bin/tar/tests/tartest.sh | the header formats, and the hard-link list a create run holds: 64 distinct multi-linked inodes archived in one run with no allocation refused, every further link written as an entry rather than a second copy of the file data, link identity and link count restored both by the tool under test and by the host tar, and the whole stored path named in the `-l` missing-links report, which reads an identity's path after the run. `tar.c` asserts at build time that the list node stays narrower than the header's name field, so the footprint follows the paths a run meets rather than the widest path ustar can carry; the window a run exhausts is the board's 144-kbyte `USER_DATA_SIZE`, where `brk()` refuses the growth (`sys/kern/kern_mman.c`) and `getmem()` turns the refusal into further links archived as full copies |
| usr.bin/textbox/tests/run.sh | the sbase text tools against GNU coreutils and sharutils, and getline_test over the tree's own getline.c: buffer ownership after a refused growth, the byte that did not fit pushed back, the bytes before a stream error terminated, and the capacity policy held at SSIZE_MAX + 1 at the host width and at -m32 |
| usr.bin/cpio/tests/cpiotest.sh | odc archives round-tripped through the host cpio |

`check-posix-sh` runs bin/sh/tests/posix-sh.sh, the conformance harness
against XCU chapter 2, which builds the shell as a 32-bit host binary;
a case the shell does not yet answer as POSIX does is declared `xfail`
and fails the moment the shell starts producing the POSIX answer.

### Repository instruction safety

`check-agent-instructions` calibrates and checks the two-file instruction
contract. `AGENTS.md` is canonical; the regular `CLAUDE.md` contains exactly
`@AGENTS.md` followed by one newline. The canonical policy references other
documents literally, including the style proposal. The only permitted active
import edge is `CLAUDE.md -> AGENTS.md`. Extra tracked instruction entries
require an explicit policy/gate change; personal untracked client files remain
outside the gate's repository scope.

The checker reads complete Markdown files and masks same-line backtick spans
before finding active import tokens. Fenced, quoted, table, HTML and multiline
examples do not extend that exception; write an import-looking literal in a
same-line backtick span. Every other canonical import rejects the gate,
including missing destinations, cycles, private-home dependencies, repository
escapes and proposal-corpus imports. The checker enforces this repository
grammar rather than reproducing every client's Markdown or instruction parser.
Length and ordinary vocabulary remain outside the gate's verdict.

Run `${PYTHON} tools/check_agent_instructions.py --staged` before committing.
Staged mode reads Git index modes and blob contents for both files, including
the canonical policy used to decide the import graph. Working-tree repairs
cannot hide an invalid index; unstaged damage cannot invalidate a valid index.
Unresolved index stages and unreadable Git objects return infrastructure
`ERROR` (status 2); policy violations return `FAIL` (status 1). A successful
check returns `PASS` (status 0).

The regular wrapper is a compatibility choice. Its installation creates a
complete temporary regular file beside the wrapper, then renames that entry
over the symlink. Compare the already-edited canonical file's hash immediately
before and after that rename, then stage both policy and wrapper and inspect
their modes. Writing through the symlink would modify the canonical policy;
unlinking first would leave an interval without the wrapper.

Client versions, effective discovery limits and actual loaded chains require
separate client observations. The official documentation describes the
[Claude import and native AGENTS behavior](https://code.claude.com/docs/en/memory)
and [Codex instruction discovery](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
The repository gate establishes neither live loading nor a general per-file
instruction-capacity allowance. Personal deployment, telemetry preferences
and updater settings remain separate from repository correctness.

### Production configuration generation and include ordering

`check-config-generated-sync` builds `tools/config` from the tested source
revision in a private temporary root. The copied closure includes its parser,
lexer, C sources and build fragments, the RP2040 template and file/device
lists, each production `Config`, and a board-specific `files.PICO` or
`files.PICO_UART` when present. The private tree preserves the generator's
`../../conf` topology and production board identities. Each invocation owns
its generator objects, architecture stamp and generated outputs. The generator
build is serial because `lang.l` consumes the parser's `y.tab.h`; independent
gate invocations build in separate directories and may run concurrently.
The root recipes export the caller's `HOST_CC`, including command-line values
containing arguments, across the detached build boundary. Calibration requires
a deliberately failing host compiler to reject generation.

The comparison reads the tracked production Makefiles and compares their
bytes with private regeneration, including comments, ordering barriers,
options, source lists and flags. A differing output names the board and
prints a unified diff. A missing Config, source input, tracked Makefile,
generated Makefile, failed generator, generator stderr or warning diagnostic
rejects the gate. The unknown-template-directive control rejects a generator
that prints an error but exits zero without changing its output.
The checker leaves the compared inputs in place. Its calibration measures
identical input bytes and the same input-file set before and after checking.

The mutation suite changes the template, a production option, a global file
list, a board-specific file list and a tracked output independently. Missing
inputs, a failed generator and a successful empty generator also reject.
The synchronized pair, a synchronized template/output edit and an unrelated
source edit pass. Two concurrent checks require private generator builds.
The fixtures start without a kernel or `include/machine`.

`check-config-makefile` retains its separate role: `mkmakefile_test.sh` uses
the real generator with a synthetic board and generic swap configuration to
check physical CFILES line width. Production synchronization preserves each
board's actual Config instead of adapting that fixture.

`check-config-include-order` extracts the actual `SYSTEM_DEP` assignment and
include-link recipes from the template and both production Makefiles. The
generated `unix` prerequisite rule drives a small host compilation through
those same dependency groups. A controller holds configuration completion,
then include creation, and checks that the dependent group stays blocked.
Removing the first barrier must start include creation before configuration
completes; removing the second must start compilation before the include
group completes. The negative-control controller releases the prerequisite
only after the specific premature-execution event arrives. An event deadline
fails the fixture as infrastructure failure, independently of the predicted
ordering defect. The intact graph completes real host compilation.

The delayed fixtures establish the dependency mechanism. They do not
establish the exact scheduling history of the original macOS incident.
A failure on a revision containing both barriers requires investigation of
that revision's dependency paths. These host gates leave ARM execution,
emulator behavior and board observations to their separate tests.

### The kernel's printf, and why it needs a narrower host

`check-kernel-ilp32` is separate from `check-kernel` for six files
that compile or hold only at the target's width. sys/kern/sys_generic.c
is built again as rwuio_test32: off_t, size_t and u_int are all four
bytes on the target, so the vector sum that wrapped there is reproduced
only at this width, and the gate asserts those widths statically before
it runs. sys/kern/kern_sig.c casts pointers to int in issignal() and
core(); sigauth_test links it with kern_prot2.c and kern_proc.c and
judges seteuid() by what kill() does afterwards, since cansignal() reads
the effective uid from p_uid in the proc entry rather than from the u
area: a process that drops root with seteuid() is refused a SIGUSR1 to
an unrelated process owned by a third uid, is refused a uid it never
held with every field left as it was, takes root back through its saved
id, and is then allowed the signal; a broadcast from the dropped process
reaches its child and not the stranger, and SIGCONT passes to a
descendant alone. p_uid is a uid_t, as u_uid and the real uid cansignal()
compares it with are, and the gate holds it at uid 40000, above what a
16-bit field represents: a caller whose effective uid is the stranger's
real uid, and one whose real uid is the stranger's effective uid, are
each allowed the signal, and seteuid() to that uid lands in p_uid
whole.

The third file is sys/kern/kern_sysctl.c, whose vm_sysctl() takes the
extent of `vm.swapmap` as a pointer difference held in an int and whose
fill_from_u() reads a process u area through an int-valued p_addr.
sysctl_test links it and holds the CTL_VM branch to the boundary the
node promises. `vm.swapmap` hands out the mapent array
swapmap[0].m_map addresses, so the copy starts there rather than at the
struct map descriptor, whose three words are kernel addresses, and it
stops at m_limit, the last usable slot. The fixture poisons every byte
from m_limit to the end of the object and gives each entry a value no
address can take, so a copy that begins at the descriptor and a copy
that runs past m_limit each land somewhere the gate names: the first
puts the descriptor's own bytes in the output, the second puts poison
there. The size query and the copy derive one length, so a caller that
sizes a buffer from the query reads exactly that many bytes; a write
attempt takes EPERM before any copy; every name at the level is
terminal; and the ids the header leaves unnamed, id 4 among them, are
refused. Two inverted sources calibrate that part: the pre-patch base
pointer fails 15 of 560 checks and a length two entries past m_limit
fails 21.

The same gate carries the length contract sysctl(3) states, which the
eight helpers and __sysctl() share: a buffer shorter than the value
takes the prefix that fits, *oldlenp reports the length the value needs
rather than the length copied, and __sysctl() compares the two to reach
ENOMEM after the node has had its say. Each helper answers all three
questions -- what a size query reports, what a short buffer takes and
reports, and what an exact buffer delivers -- because the four that once
assigned *oldlenp inside `if (oldp)` left a size query's length
untouched, so a caller sizing a buffer from it allocated whatever it had
passed in. The end-to-end cases drive __sysctl() through u_arg: an ample
buffer succeeds, a short one reports ENOMEM with the length the value
needs, a third call sized from that answer succeeds, a size query never
reaches ENOMEM however small the number passed in, and a node's own
refusal stands ahead of the truncation check. Two inverted sources
calibrate this part: the pre-patch helpers fail 43 of 1137 checks, and
helpers that report the length copied rather than the length needed fail
26 -- the syscall's comparison among them, since a reported length that
never exceeds the buffer can never be seen to overflow it. Removing the
comparison itself needs no gate run, because savelen is then set and
unused and the build refuses it.

The writable `sysctl_struct()` helper requires an exact-sized replacement,
like `sysctl_int()` and `sysctl_long()`. Its cases advertise lengths zero,
one, size minus one, the exact size and size plus one over fully allocated
input storage. A wrong size returns EINVAL before either copy and preserves
the destination and supplied output length. An exact input replaces the
whole value even when the old-value buffer is short, while the output copy
stays bounded and reports the required length. A null input remains a read
or size query regardless of the advertised input length. The copy doubles
record invocation counts and requested extents. The original greater-than
comparison rejects only oversized inputs; the expanded tests reject its
undersized copies through assertions rather than a host memory fault.

Configurable failures exercise helper copyout and copyin, syscall name and
length copyin, value transfers, final length copyout, and node-error
precedence. The authorization double can deny a write with EPERM before
dispatch, copying or mutation; reads and permitted writes have separate
success cases. The failure doubles stop before copying, so their results
cover error propagation and ordering rather than partial-copy atomicity.
The two actual-source binaries, `sysctl_test_compact` and
`sysctl_test_wide`, execute as `kernel.sysctl-compact.ilp32` and
`kernel.sysctl-wide.ilp32` in the required `firmware/posix-sh` CI job.
The source search `rg -n 'sysctl_struct' sys tests/kernel` locates the
helper definition, declaration and test calls; a production caller must
be established separately before claiming a reachable syscall defect.

The fourth file is sys/kern/ufs_alloc.c. `struct dinode` is an on-disk
layout and INOPB is MAXBSIZE divided by its size, so a block holds
exactly INOPB inodes only where off_t and time_t are four bytes;
ialloc_test asserts both statically before it runs. The gate links
ufs_alloc.c against an i-list it owns and drives it to exhaustion,
which is where the allocator's cache invariant is visible:
fs_inode[0 .. fs_ninode) holds distinct inode numbers, and neither
ifree()'s append nor the allocator's pop tests for membership. The
refill scans the i-list twice, the first pass over [fs_lasti, fs_isize)
and the second over [1, fs_isize), and the second stops as soon as the
cache is full, so an entry appears twice only on a filesystem holding
fewer than NICINOD free inodes in total -- one near exhaustion, which
is when the second pass runs at all. The fixture is nine free inodes
across three blocks with fs_lasti mid-list, and the cost is exact:
nine allocations spend nine iget() calls where the cache is distinct.
A second fixture holds the regime above the cache bound, where no entry
is duplicated because the second pass fills before it reaches fs_lasti:
there the reset changes which inodes are cached, to the NICINOD lowest,
and costs one further block read to reach that many from inode 1. The
gate states both, so the refill's price is recorded rather than
implied.

Two inverted sources calibrate it. Carrying the first pass's count into
the second fails 27 of 220 checks and spends 15 iget() calls for the
same nine inodes, six of them on entries the first pass had already
recorded. A second pass that covers no new ground fails 29 of 149 and
allocates six inodes rather than nine, stranding the three below
fs_lasti, which is what holds the reset to discarding the count rather
than the entries. The gate runs in all three shapes the tree
configures the allocator in: the portable one, PICO's (`SINGLE_UFS_ROOT`
reaches the superblock through mount[0] and `COMPACT_INODE_FIELDS`
packs the in-core flag words) and a DIAGNOSTIC kernel, whose itoo()
assertion holds fs_lasti to the first inode of a block.

The same file carries a second gate, balloc_test, over the block
allocator. `struct fs` is an on-disk layout that fills exactly one
DEV_BSIZE block, so balloc()'s `*fps = *fs` writes one block only where
daddr_t, ino_t, time_t and int are four bytes, and the gate asserts that
size statically before it runs. What it states is the meaning of the
superblock write balloc() makes when the cached free list empties and
the next chunk is read back: the image reaching the disk is clean and
carries the current time, while the in-core superblock stays modified
and keeps the fs_time ufs_sync() last wrote. Both halves are
constrained from outside the allocator. sync() (sys/kern/ufs_subr.c)
passes over a filesystem whose fs_fmod is zero without flushing an inode
or a data block, so a refill that cleared the flag in core would leave
fs_tfree, the dirty inodes and the dirty buffers held back until the
next allocation or free set it again -- and on the two paths that reach
the allocator's refusal from there, nothing does. mountfs()
(sys/kern/ufs_mount.c) keeps the on-disk fs_fmod on a read-only mount
and ufs_sync() (sys/kern/ufs_syscalls2.c) answers a set flag on a
read-only filesystem with panic("sync: rofs"), so the image may not
carry the flag set either. fs_time is the port's clock across a reboot,
since there is no time-of-day hardware and main()
(sys/kern/init_main.c) seeds time.tv_sec from the root superblock once
mountfs() returns; ufs_sync() is the write that stamps it, being the one
that flushes the inodes and the data blocks beside the superblock.

The fixture is a three-entry superblock list whose last entry addresses
a chunk of three more, so five blocks are handed out in all and the
refill falls in the middle. The gate takes the refill in both write
modes, reads the image back off its synthetic disk, drives the list to
exhaustion by two routes -- the end-of-list zero and a chunk that comes
back empty on the far side of the superblock write -- and covers the
bad-block skip and free()'s spill of a full cache into the block being
freed. Against sys/kern/ufs_alloc.c with the flag and the stamp applied
to the in-core superblock rather than to the image, 4 of 101 checks
fail in each shape: the in-core fs_time advances at a write that
flushes no inode, and both exhaustion routes that pass through a clean
superblock return with the filesystem marked clean. The gate runs in
the portable shape and in PICO's.

The fifth file, sys/kern/subr_prf.c, carries its own
argument walk,

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

The sixth file is sys/kern/ufs_fio.c. `ufs_explicit_time_test` links
`ufs_setattr()` from that source and proves at the target's width that
`MNT_NOATIME` leaves an explicit atime request, a combined atime and mtime
request, the no-timestamp case, and a read-only refusal distinct. A generated
copy restores the inherited atime guard and must fail the same assertions.

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
| `check-control-char-contracts` | Cribbage and tip compile their exact control-character consumers for Cortex-M0+; source assertions bind redraw, bell, exit, both suspend modes, literal-next and raise to distinct bytes, rejecting the inherited macros that collapsed the Cribbage actions to control-X and every tip action to control-C |
| `check-divider` | neither linked kernel reaches the SIO divider registers, from the ELF and from the dependency files, whose comments are stripped before the scan so a file may say which registers the boot ROM writes without being read as writing them |
| `check-swapram` | vm_swap.o, exec_hsaout.o and kern_sysctl.o agree with each kernel's Config on the SwapRAM tier and pool size |
| `check-cache-footprint` | the name, buffer and inode caches' ABI and chain invariants, the exec argument spool over modeled SwapRAM, raw swap and buffers (including the swap cursor's rotation, publication and wrap), and the evacuation model |
| `check-exec-spool` | sys/kern/exec_subr.c spooling arguments through the real sys/kern/subr_rmap.c reservation, over modeled SwapRAM and buffers |
| `check-ufs-prototypes` | every UFS function the kernel links has a prototype |
| `check-hsaout` | the packed a.out container against header and stream corruption, truncation and forged lengths, over every image in the distribution tree |
| `check-elf2aout` | tools/elf2aout over the layouts the ARM linker produces: seven section combinations, four rejection controls, an overwrite shorter than the file it replaces, symbol conversion, the multicall BSS overlay with its object rejection, and the overlay's rebuild freshness. It assembles and links its own fixtures with the cross toolchain and reads lib/elf32-arm.ld, so `tools` and the toolchain are the whole prerequisite and it passes on a bare checkout |
| `check-libc-contracts` | the focused libc contract gates together, followed by the board libc's a.out contracts after a rebuild from clean |
| `check-libc-malloc` | lib/libc/gen/malloc.c, calloc.c and lib/libc/arm/sys/sbrk.c compiled from the tree over an sbrk() and _brk() the test owns, so exhaustion is a ceiling the test sets: a refused realloc() leaves the block allocated, shown by allocating again and requiring the result outside it; an oversized malloc(), calloc() or realloc() is ENOMEM before any arena call; calloc() rejects a product that wraps; growth into free neighbors and shrinking stay in place; a moved block frees its origin; 4000 mixed operations keep every live pattern; sbrk() returns -1 with brk's errno on refusal. The same binary runs at -m32 where the host can build it, which is the target's width. Against the allocator before the change the preservation phase fails and the oversized resize hangs, and sbrk.c before the change returns the old break on refusal |
| `check-libc-qsort` | lib/libc/gen/qsort.c compiled from the tree under another name: a sort of 48-byte records whose comparator sorts an array of ints before answering, which an implementation with file-scope record size and comparator finishes with the inner call's values; ordering against a reference at record sizes 1, 2, 4, 7 and 48 for counts from 0 to 300 around the insertion threshold; equal, sorted, reversed and three-key inputs; and size 0 or n under 2 as a no-op. Host width and -m32 |
| `check-libc-strtox` | strtol and strtoul compiled as strict C17 at host and ILP32 widths: unsigned-byte ctype arguments, invalid bases, incomplete hexadecimal prefixes, end pointers, overflow, deterministic portable-character-set cases and a host-libc differential for inputs that perform a conversion. Against the pre-change sources, 7 of 51 focused checks fail; the corrected sources pass all 51 checks at each width |
| `check-libc-printf` | the bounded string formatter contract at host and ILP32 widths; the host-width compile also rejects pointer narrowing in `%p` |
| `check-libc-printf-float` | the same formatter with `doprnt_float` linked: `%a` and `%A` exact digits, precision rounding to even with a carry into the exponent, the sign of a negative zero, and `%f` and `%e` |
| `check-libc-scanf` | the formatted input scanner's directive, boundary and malformed-input contract at host and ILP32 widths, plus an address-sanitizer run that is the only tier able to see a scratch overrun inside the scanner's own frame |
| `check-libc-rwmode` | the C17 7.21.5.3p7 mode switch on an r+ stream at host and ILP32 widths |
| `check-libc-syslog` | the bounded logfile and stderr record contract over the tree's string formatter |
| `check-libc-vis` | the legacy and bounded visual encoders compiled as strict C17 with signed input bytes: all 65,536 byte, flag and octal-lookahead combinations preserve the historical encoding; exact and short capacities, destination guards, an unchanged destination on ENOSPC, embedded NUL input and high-bit bytes pin the bounded contract |
| `check-libc-string-security-cross` | Cortex-M0+ disassembly of timingsafe_bcmp has two volatile byte loads, one length-controlled conditional branch and no delegation to bcmp or memcmp; an early-return comparator calibrates the rejection path |
| `check-dirent-contracts-cross` | the five exact directory-stream members compile as strict C17 for Cortex-M0+; the target layout is 1,040 bytes, the 1 KiB directory block remains intact, and telldir has no lseek dependency |
| `check-cat-contracts-cross` | the exact cat source compiled as strict C17 for Cortex-M0+ with full warnings; its static assertions bind the transfer buffer to the target's BUFSIZ, MAXBSIZE and DEV_BSIZE |
| `check-rmdir-contracts-cross` | the exact rmdir source compiled as strict C17 for Cortex-M0+ with full warnings |
| `check-tee-contracts-cross` | the exact tee source compiled as strict C17 for Cortex-M0+ with full warnings |
| `check-du-contracts-cross` | the exact du source compiled as strict C17 for Cortex-M0+ with full warnings |
| `check-find-contracts-cross` | the exact bigram and code sources compiled as strict C17 for Cortex-M0+ with full warnings, which usr.bin/find's own `WARNLEVEL=legacy` leaves to this gate |
| `check-backgammon-contracts-cross` | the exact subs.c compiled as strict C17 for Cortex-M0+ with full warnings, which `games/backgammon` itself now builds under after the 14 sites the directory carried were repaired |
| `check-resize-contracts-cross` | the exact resize source compiled as strict C17 for Cortex-M0+; the linked utilbox contains no ctype table, formatted-input scanner or scanf entry point, and a fixture containing every forbidden symbol calibrates the rejection path |
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

The login test reads `kern.capacity` first and asserts the versioned ABI size,
all five retained fixed-table limits, and the 3,072-byte u-area geometry. The
dynamic occupancy and watermark values remain observations from one emulator
workload rather than evidence for smaller limits. The login test then ends
with the MPU's deliberate fault test. The probe test
asserts the `mpu:` banner line, which is MPU_TYPE and MPU_CTRL read back
after sys/arch/rp2040/rp2040/mpu.c programmed its three regions; the login
test then runs usr.bin/mputest, which forks children that read kernel RAM,
kernel text and SIO CPUID from unprivileged code and expects SIGSEGV for
each while the divider beside that CPUID reads and divides, and asserts
`MPUTEST OK (mpu on)`. `mputest residue` follows: it fills the span
between the image and its stack, execs, and asserts `RESIDUE OK`, which
holds only when exec_clear has zeroed that span. Renode's Cortex-M0+ implements the
region registers, so the emulator decides the register-level claim and the
fault path; the board run decides the silicon. MPU.md has the map.

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
text restoration), swapmaptest (the swap map read through sysctl(3), each
free run held inside the device between SWAP_IMAGE_ALIGN and vm.nswap),
epochtest, evactest, bigtest and hugetest (the
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
