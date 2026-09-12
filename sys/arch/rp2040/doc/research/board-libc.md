# Board-native link library for DiscoBSD/RP2040

Worktree: `~/worktrees/discobsd/board-libc`, branch `board-libc`, off
`rp2040-port` at `3fcbdb41`. Board not touched; every check below runs on
the host against the tree's own as/ld/ar (`tools/bin/*`, host binaries
built from `usr.bin/{as,ld,ar,ranlib}`) and Unicorn.

## Result

- `distrib/rp2040/mkboardlibc.py` builds `${DESTDIR}/usr/lib/libc.a`: 89
  archive members (see `distrib/rp2040/boardlibc-members`), **36412 bytes**
  (36.4 KB), well under the 75 KB target -- the full `lib/libc.a` this was
  cut from is 331932 bytes.
- `${DESTDIR}/usr/lib/crt0.o`: 192 bytes, from the existing
  `lib/libc_aout/startup` a.out build (unreduced -- one small object).
- `/usr/bin/as` a.out: 32369 bytes text+data. `/usr/bin/ld` a.out: 22673
  bytes text+data. Together 55042 bytes, matching the task's "55 KB
  together" figure.
- New root additions total libc.a + crt0.o + cc (2837 bytes) =~ 39.4 KB,
  leaving headroom in the ~90 KB free after as+ld land.

## How the closure was found

`distrib/rp2040/libc-sink.c` calls the everyday API: stdio without float,
string, ctype, malloc, the common syscalls, signal, exec, wait, stat,
time, errno/perror, qsort, the strtol family, getenv. Every call feeds a
`volatile long sink` so a compiler that recognizes a libc name as a
built-in (`strcmp`, `memcmp`, ...) cannot fold the call away when its
result goes unused.

```
CC="arm-none-eabi-gcc -std=gnu17 -mcpu=cortex-m0plus -mabi=aapcs \
    -mlittle-endian -mthumb -mfloat-abi=soft -nostdinc -Iinclude"
$CC -Os -fcommon -fno-builtin -c distrib/rp2040/libc-sink.c -o sink.o
LDFLAGS="-N -nostartfiles -fno-dwarf2-cfi-asm -Wl,--no-warn-rwx-segments \
    -Tlib/elf32-arm.ld lib/crt0.o -Llib"
$CC -Os $LDFLAGS -o sink.elf sink.o -lc -Wl,-Map=sink.map
grep 'libc\.a(' sink.map | sed -E 's/.*libc\.a\(([^)]+)\).*/\1/' | sort -u
```

`-fno-builtin` on this compile only (not on the board's own flags) matters:
without it, GCC treats `strcmp`/`memcmp`/`strchr`/`toupper`/... as builtins
and deletes calls whose result is discarded, along with everything they
would have pulled from the archive -- the very functions the closure needs
to name. 89 members came back, all named in `boardlibc-members`.

## Building the reduced archive

`distrib/rp2040/mkboardlibc.py` classifies each member by which of libc's
`.PATH` directories it lives in (`lib/libc/{gen,stdio,stdlib,string,
compat}`, `lib/libc/arm/{sys,gen,string}`), then for each:

- **A `.c` source**: `arm-none-eabi-gcc -S` with the tree's exact libc
  flags (`bmake -n MACHINE=rp2040 -C lib/libc`) plus `-fno-jump-tables`
  (ARMv6-M has no divide or jump-table instruction, and the board carries
  no libgcc to supply `__gnu_thumb1_case_*`), then the tree's own `as`
  assembles the `.s`.
- **A hand-written `.S`** (`aeabi_div.S`, `memmove.S`/`strcmp.S`'s arm
  overrides, `_exit.S`/`_brk.S`/`pipe.S`/`ptrace.S`/`sigaction.S`): `gcc -E
  -P -x assembler-with-cpp` expands the SYS.h-style macros to plain Thumb-1
  text, then the tree's `as` assembles it.
- **A syscall stub** (the `SYSOBJS` in `lib/libc/arm/sys/Makefile`, no
  source file): the same `#include "SYS.h"\nSYS(name)\n` snippet
  `lib/libc/arm/sys/Makefile` pipes into `$(AS)`, preprocessed the same way.

All 89 `.o` files are archived with the tree's own `ar` (`rc`) and indexed
with the tree's own `ranlib` -- a.out format end to end, since `usr.bin/ld`
on the board reads a.out only.

## What was cut

- **`picoc`**: dropped from the rp2040 build and manifest (67 KB), per the
  task. `usr.bin/Makefile`'s `_PICOC` variable keeps every other machine's
  `SUBDIR` unchanged.
- **Floating-point printf conversion** (`%e`/`%f`/`%g`): `doprnt.c`'s float
  branch compares a `double` against zero and classifies it with
  `isnan`/`isinf` unconditionally, before ever checking whether
  `__doprnt_cvt` (the weak entry `doprnt_float.c` supplies) is linked in.
  `arm-none-eabi-gcc` needs `__aeabi_dcmp*`, `__eqdf2` and `__ledf2` for
  that comparison; those live in `lib/libc/runtime/comparedf2.c`
  (compiler-rt derived, MIT/Apache-2.0, **not** GPL libgcc) but `runtime/`
  is not in `lib/libc/Makefile`'s subdirectory list for arm at all, so
  none of it is in `lib/libc.a` today. A host link with the gcc driver
  masks this: `-lgcc` is implicit and supplies `__eqdf2`/`__ledf2` ahead
  of anything `-lc` could offer, silently hiding the gap (confirmed by
  reading the same `-Wl,-Map`: the winning objects are
  `libgcc.a(eqdf2.o)`/`libgcc.a(ledf2.o)`, not `lib/libc.a`). The board's
  own `usr.bin/ld` link has no such fallback. Fix: `lib/libc/stdio/
  doprnt.c` gains a `NO_DOPRNT_FLOATFMT` compile-time guard that skips
  straight to the same `'?'` output `__doprnt_cvt == 0` already prints
  elsewhere in the function; `mkboardlibc.py` defines the macro only for
  its own `doprnt.c` compile. No other machine defines it, so every other
  libc build is unchanged. `%d`/`%s`/`%x`/etc. printf conversions are
  unaffected -- confirmed by acceptance test 1.
- **Single-precision float arithmetic** entirely: `t16_float` (smlrc's own
  test suite) fails to link against the reduced archive with `__aeabi_fmul
  __aeabi_fdiv __aeabi_f2iz __aeabi_fsub __aeabi_fadd __lesf2 __gesf2
  __aeabi_fneg __aeabi_i2f` undefined. These live in `lib/libc/runtime`
  (`addsf3.c`, `mulsf3.c`, `comparesf2.c`, ...) which is not built into
  `lib/libc.a` (same gap as above) and is not part of the sink's declared
  scope (no float functions in libc-sink.c). Named here as the scope cut
  the task's float-branch instruction implied but did not itself close:
  a board program that does floating-point *arithmetic* (not just
  printf's `%f`) needs `lib/libc/runtime` added to `lib/libc/Makefile`'s
  subdirectory list and to `boardlibc-members` -- not done, out of scope
  for a sink that only exercises the categories named in the task.

## The `aeabi_div.S` fix

`lib/libc/arm/gen/aeabi_div.S` (added to the tree in this branch, per the
task's setup) used `bhs`/`blo`, the ARM UAL aliases for `bcs`/`bcc`.
`usr.bin/as/as-thumb.c`'s `condtab` lists only the eight base ARMv6-M
condition codes plus `hi ls ge lt gt le al` -- no `hs`/`lo` -- so the
tree's own assembler rejected the file with "bad instruction" while GNU
`as` accepted it silently. Both aliases name the same encoding as
`bcs`/`bcc`; swapping the mnemonics is a no-op semantically and the file
now assembles with the tree's `as`.

## A real bug found and fixed in the test harness (not the board)

`usr.bin/as/tests/thumb-run.py`'s `fstat` stub wrote a fixed 128 bytes
into the caller's buffer regardless of its actual size. `struct stat`
(`include/sys/stat.h`) is 14 4-byte members, 56 bytes; `lib/libc/stdio/
flsbuf.c` stack-allocates exactly `sizeof(struct stat)` (`sub sp, sp,
#68` for the whole function, `add r1, sp, #8` for the struct itself, per
the compiled `.s`) the first time a `FILE`'s buffer needs sizing. Writing
128 bytes there landed 72 bytes past the struct, onto `_flsbuf`'s own
saved `{r4,r5,r6,r7,lr}` and its caller's frame -- the program's own
buffered stdout write zeroed its return address. Unicorn's `until=0`
sentinel then read as "reached PC 0, stop clean" with no fault reported,
so `thumb-run.py` printed nothing and exited 0: a silent pass that was
actually a stack-smashing crash. Confirmed with a capstone instruction
trace (`_flsbuf`'s epilogue `pop {r4,r5,r6,r7,pc}` executing right after
the corrupting write) and independently by reading `sys/kern/
kern_descrip.c`'s real `fstat()`, whose `copyout` is already exactly
`sizeof(ub)` -- the kernel never over-writes, so no real board program
ever hits this path; it is purely a test-harness artifact. Fixed by
writing 56 bytes instead of 128. Also added minimal `open`/`lseek`/
`execve` stubs (fixed fake fd, no-op, and a reported ENOEXEC with carry
set respectively) so acceptance test 2's fopen/fgets/execve calls have
somewhere to go instead of stopping the run at the first unimplemented
syscall.

## Commands and evidence

```
$ python3 distrib/rp2040/mkboardlibc.py --workdir /tmp/w --out /tmp/w/libc.a
mkboardlibc: 89 members, 36412 bytes
```

Full member list: `distrib/rp2040/boardlibc-members` (89 lines).

### Acceptance (1): smlrc, integer division and printf

`distrib/rp2040/tests/accept-div-printf.c`, compiled with the host smlrc
(`-DTHUMB`), assembled with `tools/bin/as`, linked with `tools/bin/ld`
against the reduced `crt0.o`/`libc.a`:

```
$ tools/bin/ld -o t1.aout lib/libc_aout/crt0.o t1.o boardlibc.a
$ python3 usr.bin/as/tests/thumb-run.py t1.aout
355 / 113 = 3 rem 16
```

Zero undefined symbols at link (`ld` printed nothing and exited 0).

### Acceptance (2): gcc -S/as, fopen/fgets/strtol/qsort/malloc/exec

`distrib/rp2040/tests/accept-stdio-exec.c`, compiled with `arm-none-eabi-gcc
-S -fno-jump-tables`, assembled and linked the same way:

```
exec errno 8
empty file
sorted: 1 2 3 4
strtol: 123
```

`exec errno 8` is `ENOEXEC` from `thumb-run.py`'s new execve stub (there is
no program image to load under Unicorn); `empty file` is `fgets` correctly
reporting EOF from the fake opened fd, since `read` always returns 0. Both
are the expected, defined outcomes of the harness's stubs, not failures.

### Acceptance (3): both run under Unicorn -- shown above, exit 0 both times.

### Acceptance (4): `bmake MACHINE=rp2040 -C usr.bin/smlrc test`

```
PASS t01_arith ... PASS t16_float
passed 16, failed 0, link-only 0
```

All 16 pass (this suite links against the gcc driver's implicit `-lgcc`,
not the reduced board archive, so it does not by itself prove the
board-library closure -- see the smlrc/board-pipeline cross-check below).

### smlrc-against-the-reduced-archive cross-check (beyond the stated acceptance list)

Pushed four more of smlrc's own test programs through the actual board
pipeline (host smlrc -> `tools/bin/as` -> `tools/bin/ld` against the
89-member archive, not the gcc-driver link `tests/run.sh` uses) to check
for helpers smlrc's Thumb-1 back end might need that libc-sink.c's gcc
compile never surfaces (e.g. `switch` dispatch):

```
t05_switch  -> links clean, 0 undefined
t11_types   -> links clean, 0 undefined
t14_stress  -> links clean, 0 undefined
t15_divreg  -> links clean, 0 undefined
t16_float   -> Undefined: __aeabi_fmul __aeabi_fdiv __aeabi_f2iz
               __aeabi_fsub __aeabi_fadd __lesf2 __gesf2 __aeabi_fneg
               __aeabi_i2f
```

`cgthumb.c` never emits a call to `__sc_case`/`sc_case.S` (that helper is
MIPS-only, guarded by `#if __mips__`); smlrc's Thumb back end compiles
`switch` to inline compare-and-branch chains, so no extra libc member is
needed for it. `t16_float`'s failure is the float-arithmetic scope cut
named above, not a defect in the closure.

### Acceptance (5): `bmake MACHINE=rp2040 fs`

```
$ bmake MACHINE=rp2040 fs
...
Created filesystem at partition 1 of .../sdcard.img - 949 kbytes
Installed 17 directories, 63 files, 19 devices, 27 links, 1 symlinks
$ echo $?
0
```

Ran with only this task's own five new files actually populated in
DESTDIR (`usr/lib/libc.a`, `usr/lib/crt0.o`, `usr/bin/as`, `usr/bin/ld`,
`usr/bin/cc`, via their new `distrib/rp2040/Makefile.inc` rules and
`usr.bin/as`'s own install target) -- not a full `bmake build` of every
program in the manifest, which is far outside this task's scope and this
session's time budget. `fsutil` tolerates a manifest entry whose source
file is absent (it warns per file and continues), so `fs` succeeds either
way; what a *full* `build` pass would additionally need is confirmed
separately: `usr.bin/ld`'s `install` target depends on a `$(MAN)` built
with `mandoc`, which this host does not have (`tools/config`'s own build
hit the same gap earlier). `usr.bin/as` has no such dependency and
installed cleanly. Not run: a full `bmake MACHINE=rp2040 build`.
Verified with `fsutil --verbose --partition=1`:

```
/usr/bin/as - 32401 bytes
/usr/bin/ld - 22705 bytes
/usr/bin/cc - 2837 bytes
/usr/lib/libc.a - 36412 bytes
/usr/lib/crt0.o - 192 bytes
```

### Regression: `usr.bin/as/tests/thumb-link.sh` (proves the full 329-member library, not just the 89-member cut)

```
$ TOPSRC=$(pwd) MACHINE=rp2040 \
  CC="arm-none-eabi-gcc -std=gnu17 -mcpu=cortex-m0plus -mabi=aapcs \
      -mlittle-endian -mthumb -mfloat-abi=soft -nostdinc -I$(pwd)/include" \
  sh usr.bin/as/tests/thumb-link.sh
link: 329 libc units assembled, 0 disagree with arm-none-eabi-as
link: text 1616 data 436 bss 72, entry 0x7f008001, no relocation left
link: the program ran and printed what it should
```

Passes after the `aeabi_div.S` mnemonic fix, the `doprnt.c`
`NO_DOPRNT_FLOATFMT` guard (inert here -- no machine defines the macro),
and the `thumb-run.py` fstat/syscall changes -- none of the three change
this test's own path or its result.

## Not run

- A full `bmake MACHINE=rp2040 build` (every usr.bin program, kernel,
  etc.) -- outside this task's scope and this session's time budget;
  `usr.bin/ld`'s man page needs `mandoc`, absent on this host.
- Anything on the physical board: not touched, per the task.
- `bmake -C usr.bin/smlrc fuzz` (needs `qemu-arm`): not attempted, not
  part of the stated acceptance list.

## Commit list (branch `board-libc`, off `rp2040-port` at `3fcbdb41`)

1. `5ce37759` libc/arm: add AEABI integer division helpers for the RP2040
2. `814ec6f8` libc/arm: use bcs/bcc in aeabi_div.S, not the hs/lo aliases
3. `3c32b0dd` libc/stdio: cut doprnt's float branch at compile time for NO_DOPRNT_FLOATFMT
4. `a6821fb0` distrib/rp2040: add the board libc member closure and its builder
5. `1d91d905` distrib/rp2040: install the board's own libc.a, crt0.o and cc
6. `0a763199` distrib/rp2040: add cc, the board's native compile driver
7. `4d248751` distrib/rp2040: add the board libc acceptance test sources
8. `48d0f4c8` as/tests: extend thumb-run.py's syscall stubs and fix fstat's size
9. `4ca80d99` usr.bin, distrib/rp2040: build as/ld/cc for rp2040, drop picoc
10. `637e09f0` distrib/rp2040: reset the manifest default section before filemode

Acceptance (5)'s `bmake MACHINE=rp2040 fs` and `fsutil --verbose` output
above were re-run from this exact HEAD (`637e09f0`, clean working tree)
after commit 10 fixed a manifest-parser rejection that commit 9 had
introduced (`filemode` needs a preceding `default` line once any `file`/
`dir` entry has opened a section; the earlier working-tree fix predated
its own commit). `git status --porcelain` is empty at HEAD.
