# 2.11BSD patches 432 to 499 against this tree

## Question

DiscoBSD's userland descends from 2.11BSD through RetroBSD. 2.11BSD has kept
shipping patches, patch 499 of January 2026 being a libc and stdio
modernization of exactly the code this tree still carries. Three things follow
and this report settles them: where the descent forked, which upstream patches
since that point touch code this tree still has, and what adopting patch 499's
stdio would cost a 144 KB process window.

## Identity

| Surface | Value |
| --- | --- |
| Comparison base | `728677b8ff4330decf579b8ea03693ee904bb592`, clean at baseline build |
| Migration result | The commit containing this report, compared through clean final a.out builds |
| Upstream | a 2.11BSD git checkout whose base is the TUHS patch-level 431 tape and whose history carries one commit per patch to 499 |
| Upstream announcement | `http://www.2bsd.com/2.11BSD/499`, fetched over plain HTTP 2026-09-19; HTTPS to that host refuses the connection |
| Cross compiler | `arm-none-eabi-gcc` 16.2.0 |
| Host compiler | `gcc` 16.2.1 |
| `bmake` | 20260824 |
| Build | `bmake MACHINE=rp2040 clean` then `bmake MACHINE=rp2040 build` for the comparison base and migration result, exit 0, zero compiler warnings |

Evidence ranks follow `AGENTS.md`. Nothing here is a board result: no claim in
this report reached hardware, and every runtime verdict is a host or
cross-compilation result, labeled as such.

## Reproducing the mapping

`tools/analysis/bsd211_patch_scope.py` produces the tables below. It takes a
2.11BSD git checkout, and for each patch commit intersects the files that
patch touches with this tree, then compares this tree's copy against the
upstream parent and child blobs of that commit:

    BSD211=<2.11BSD checkout> ${PYTHON} tools/analysis/bsd211_patch_scope.py \
        --base <tape revision> --format table

Distance is differing lines after trailing-whitespace normalization, so tab
and line-ending drift between the trees does not decide a verdict. The
distance from the upstream *parent* is what makes a verdict usable: a file at
parent distance 0 is the upstream pre-patch file verbatim and the upstream
hunk applies to it directly, while a file hundreds of lines away was rewritten
here and the upstream hunk is advisory at best. The report classifies parent
distance at or below 40 as applicable, at or below 150 as partial, and beyond
that as rewritten.

The method has a known limit: convergence is not descent. A file this tree
independently changed the same way upstream did reads as "carried" without any
patch having been applied. Patch 499's 35 "carried" files are almost all
Makefiles where both trees dropped `DESTDIR` and adopted `mkdep`, which is
convergence, not inheritance. Treat "carried" as "needs no action", not as
"the patch was applied".

## Where the descent forked

**The fork is at patch level 431 or earlier, and no patch from 432 onward has
been applied.** Two independent observations settle it.

First, files this tree carries are byte-identical to the upstream *pre-patch*
text for patches spread across the whole range, from 432 to 499:

| Upstream patch | File | Parent distance |
| --- | --- | ---: |
| 432 | `lib/libc/net/res_send.c` | 0 |
| 432 | `lib/libc/net/named/gethnamadr.c` | 0 |
| 447 | `usr.sbin/pstat/pstat.8` | 0 |
| 460 | `games/hunt/hunt.c`, `hunt.h`, `execute.c`, `playit.c` | 0 |
| 487 | `lib/libc/net/rexec.c` | 0 |
| 489 | `games/warp/warp.news` | 0 |

A file at parent distance 0 for patch 487 cannot have received patch 432
either, because these are the untouched 431-era texts.

Second, the upstream repository's own base is the patch-level 431 TUHS tape,
and the earliest patch in its history that touches anything this tree carries
is 432 -- the first patch after that tape. There is no window in which an
intermediate patch level could have been the fork point and left no trace.

The practical consequence: **68 patches of upstream fixes have never been
evaluated against this tree.** Most do not matter, as the next section shows,
but the ones that do have been sitting unexamined.

## Scope of the 68 patches

Of 89 upstream commits between the tape and patch 499, **48 touch no file this
tree carries at all.** They are PDP-11 kernel drivers, VAX network interfaces,
`roff`, `tcsh`, `pascal`, `sendmail`, `uucp` transports and the `man0`
tree -- subsystems this port does not have.

Commits that do touch shared files:

| Patch | Upstream files | Shared | Needs no action | Missing here | Undecided |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 432 | 25 | 3 | 0 | 2 | 1 |
| 444 | 3 | 1 | 0 | 1 | 0 |
| 446 | 10 | 1 | 0 | 1 | 0 |
| 447 | 11 | 3 | 0 | 2 | 1 |
| 452 | 24 | 16 | 2 | 13 | 1 |
| 456 | 10 | 4 | 0 | 4 | 0 |
| 457 | 10 | 4 | 0 | 4 | 0 |
| 460 | 130 | 17 | 5 | 5 | 7 |
| 477 | 16 | 4 | 0 | 2 | 2 |
| 478 | 17 | 8 | 3 | 2 | 3 |
| 480 | 8 | 3 | 0 | 3 | 0 |
| 482 | 47 | 7 | 0 | 4 | 3 |
| 484 | 19 | 3 | 0 | 2 | 1 |
| 487 | 22 | 5 | 0 | 5 | 0 |
| 489 | 26 | 7 | 1 | 4 | 2 |
| 499 | 962 | 133 | 35 | 57 | 41 |

Patches 442, 481, 483, 488, 493, 496 and 498 each touch one or two shared
files and need nothing. The full table, including the commits whose subjects
carry no patch number, comes from the script above.

Two patches deserve names. **Patch 460, "2.11BSD completely lacks ANSI C
capability"**, is the upstream ANSI groundwork that precedes 499: 130 files,
17 of them shared here, adding prototypes and the headers 499 then depends on.
**Patch 452** corrects `srandom(3)` and `initstate(3)` argument handling and
touches 16 shared files, mostly games that seed from the corrected interface.

### Upstream fixes that apply cleanly and are absent here

These are files this tree carries close to the upstream pre-patch text, so the
upstream hunk is directly usable. Listed by parent distance, nearest first.
This is an inventory, not a recommendation: several are for subsystems this
port does not ship.

| Patch | File | Parent distance | Subject of the fix |
| --- | --- | ---: | --- |
| 432 | `lib/libc/net/res_send.c` | 0 | resolver cleanup |
| 432 | `lib/libc/net/named/gethnamadr.c` | 0 | resolver cleanup |
| 460 | `games/hunt/{hunt,execute,playit}.c`, `hunt.h` | 0 | ANSI prototypes |
| 487 | `lib/libc/net/rexec.c` | 0 | unused variables |
| 489 | `lib/libc/net/ruserpass.c` | 1 | unused variables |
| 487 | `lib/libc/net/rcmd.c` | 4 | gettytab handling |
| 480 | `libexec/getty/{init.c,gettytab.h}` | 16, 17 | 8-bit and parity handling |
| 452 | `games/mille/roll.c` | 9 | `srandom` argument |
| 473 | `usr.bin/uucp/uuxqt.c` | 11 | missing prototypes |
| 499 | `usr.bin/find/{bigram,code}.c` | 10, 13 | warning cleanup |
| 452 | `games/atc/include.h`, `log.c` | 25, 38 | `srandom` argument |
| 496 | `usr.bin/yacc/dextern` | 15 | yacc fix |
| 446 | `sbin/umount/umount.c` | 24 | umount fix |

The networking files are the largest group and this port has no network, so
they are inventory only. The `getty` files matter more than they look: this
port's console path descends from them.

Patch 499's own stdio entries appear in the missing list at parent distances
of 6 to 24 for `setvbuf.c`, `getchar.c`, `gets.c`, `fgetc.c`, `putchar.c`,
`ungetc.c`, `fopen.c`, `fseek.c`, `ftell.c`, `fread.c`, `fwrite.c`,
`vfprintf.c` and the rest. Those small parent distances confirm that this
tree's stdio *is* the pre-499 2.11BSD stdio with local edits, and the large
child distances -- 112 to 318 lines -- confirm that 499 replaces rather than
patches them.

## What patch 499 actually is

The announcement's own words, from the fetched text:

> The stdio package has been updated with the 4.4BSD code to be Ansi/ISO-C
> compliant. It supports everything in section 7.19 in the C99 standard
> except for the wide char routines; those make little sense to port to 2BSD.

Anders Magnusson ported Chris Torek's 4.4BSD stdio to 2.11BSD, and the Fix
section records the scale as "Over 160 files added/patched/removed". The
commit that carries it here changes 962 files, though most of that count is
the unrelated Makefile sweep and the removal of obsolete trees.

It is not a straight 4.4BSD import. The announcement lists what was changed
*away* from 4.4BSD, and each change is a small-machine concession:

- "Move code referring to extended functions to a separate struct which is
  allocated on demand." This is `struct __sfops`, holding the four I/O
  function pointers, the cookie, the `ungetc` buffer and the `fgetln` buffer.
  A stream using the default operations points at one shared `__sdefops`, so
  the per-stream static cost drops below 4.4BSD's.
- "Remove dynamic buffer size allocation functions; this makes very little
  sense on pdp11."
- "Remove some of the fseek optimizations to cut down size."

Floating conversion is split out: `e/f/gcvt` stay unaltered, a new `acvt()`
serves `%a`, and `pfcom.c` carries the formatting behind an
`asm(".globl fltused ; fltused:")` with the comment "hack to ensure pfcom only
get included if floating point used". The announcement also concedes the
rounding is incorrect and that doing it correctly is "quite heavy-weight".

The announcement is candid about cost, and states it in numbers:

| Artifact | Before | After |
| --- | --- | --- |
| `vfprintf` against `doprnt`+`strout` | text 950, data 64 | text 1262, data 98 |
| "Hello World" | text 3076, data 328 | text 3654, data 362 |

with the note that the growth is partly "because the input routines are always
present as well now."

It is also an ABI break, stated plainly: a `.o` compiled against the old
`stdio.h` "will cause an undefined symbol (`__iob`) error and halt the build",
so the whole system must be recompiled. `_iob[]` is replaced by `__sF[]` and
`FILE` becomes `struct __sFILE` with `_p`, `_r`, `_w`, `_flags`, `_file`,
`_bf`, `_lbfsize`, `_fops`, `_up`, `_ur`, `_ubuf[1]`, `_nbuf[1]` and
`_offset`. `FOPEN_MAX` is 8.

### A defect in the patch as published

`_fwalk`, as it appears in the announcement text itself:

```c
for (g = fpole; g; g->next)
        if (g->sfile._flags == 0)
                ret |= (*function)(&g->sfile);
```

Two faults in three lines, both upstream rather than artifacts of applying the
patch:

1. `g->next` sits in the increment position of the `for`, where its value is
   evaluated and discarded. `g` never advances, so the loop does not
   terminate once `fpole` is non-empty.
2. The test is inverted. `_flags == 0` marks a *free* stream -- the header
   comments the field "this FILE is free if 0", and `__sfp` claims a slot on
   exactly that test -- while the static-array loop immediately above
   correctly uses `_flags != 0`. As written, `_fwalk` would visit only the
   free dynamic streams and skip every live one.

`fflush(NULL)` calls `_fwalk(__sflush)`, and `_cleanup`, which `exit` calls,
does the same. `fpole` becomes non-empty when a program holds more than
`FOPEN_MAX` streams at once, so a 2.11BSD program that opens a ninth stream
hangs at exit or at `fflush(NULL)`. Fault 1 makes the hang unconditional;
fault 2 means the loop body would be wrong even after the advance is fixed.

This was found by reading, at evidence rank 3, and has not been executed
against a PDP-11 or SIMH. `docs/research/211bsd-fwalk-report.md` holds the
report drafted for the 2.11BSD maintainer; per `AGENTS.md` it is not sent
without an explicit request naming it.

## What 499's stdio would cost this port

The interesting question is not whether 499 is good work -- it is -- but
whether its shape fits a 144 KB process window. Three costs, all from reading
the patch text at rank 3, none measured on this target because no port was
attempted:

**Per-stream static data.** This tree's `FILE` is 20 bytes at ARM32:
`_cnt`, `_ptr`, `_base`, `_bufsiz` at 4 each, `_flag` and `_file` at 2. The
499 `struct __sFILE` laid out at ARM32 alignment is 48 bytes, plus a 36-byte
`struct __sfops` for any stream that does not use the shared default. The
static `__sF[FOPEN_MAX]` array therefore grows from 160 bytes to 384.

**Automatic frame.** `__svfscanf` declares `char ccltab[256]` and
`char buf[BUF]` with `BUF` 513, so 769 bytes of frame on every `scanf` call.
Against this port's 3 KB u-area reservation and a user stack inside the 144 KB
window, that is a large unconditional charge for a conversion.

**The ABI break.** The `_iob` to `__sF` change reaches every program, the
multicall boxes and their overlay proof included, and `lib/libcurses/scanw.c`
reaches `_doscan` directly.

None of this argues against adopting the parts of 499 that pay. It argues that
a wholesale swap needs its own measured campaign, with the overlay and
conversion gates run, rather than a transliteration.

## The first migration unit: `_doscan`

`lib/libc/stdio/doscan.c` was chosen because it is where the conformance gap
actually sits, it is independently testable, and it does not touch the `FILE`
layout, so no consumer's ABI moved and `scanf.c`, `scanw.c` and
`include/stdio.h` are unchanged.

### Defects in the scanner that was replaced

Found by reading the source, then confirmed by executing it:

1. **A staging buffer overrun.** `_innum` staged digits in `char numbuf[64]`
   while an absent field width defaulted to 30000. Both inlined into
   `_doscan`'s 128-byte frame, so the overrun reaches the return address.
   `sbin/sysctl/sysctl.c:351` converts the value of a `CTLTYPE_LONG` node
   through `%ld`, and `kern.hostid` is such a node in `sys/sys/sysctl.h`, so
   `sysctl -w kern.hostid=<65 or more digits>` reaches it with an argument the
   caller writes. The consequence is the process corrupting its own stack;
   with no MMU and one resident process there is no protection boundary
   between the caller and the damage.
2. **`%X` stored a long through a pointer to int.** `isupper(ch)` folded the
   conversion to lowercase *and* forced the long modifier, so `%X`, `%D`,
   `%O`, `%E` and `%F` all wrote a `long`. C17 7.21.6.2p12 makes `X` identical
   to `x`. This is an out-of-bounds write whenever the argument is an `int *`.
3. **`%f` and `%e` matched but stored nothing.** The conversion sat behind
   `#if HAVE_FLOAT`, and `HAVE_FLOAT` is defined nowhere in the tree, so the
   directive consumed its input, returned a success verdict, and left the
   destination untouched. Twelve call sites use `%f` or `%lf`.
4. **No `%n`, `%i` or `%p`.** `usr.bin/virus/virus.c:1704` already uses
   `"%d%n"`, which the scanner parsed as a second numeric conversion.
5. **A mutable file-scope scanset.** `static char _sctab[256]` was rewritten
   per `%[` directive: 256 bytes of writable data in every program linking
   `scanf`, and one scanset shared across directives.
6. **A writable ctype dependency.** `isdigit` and `isupper` index `_ctype_`,
   pulling that table in as well.

A seventh defect is in `ungetc`, not the scanner: `*--iop->_ptr = c` stores
into the caller's buffer, which for the `_IOSTRG` stream `sscanf` builds is
the caller's string, commonly a literal. On this target read-only data is
ordinary RAM and the store lands silently; on a host that maps literals
read-only it faults, which is how it was found. `ungetc` now skips the store
when the byte already matches, which covers every pushback a scanner performs.
A pushback of a *different* character still stores and keeps the old exposure;
removing that case needs a separate pushback buffer, which is what 499's `_ub`
provides.

### The replacement

Integer conversion accumulates into an `unsigned long` as digits arrive, so
the integer path stages nothing at all and saturates at the destination limit
where C17 7.21.6.2p10 leaves the unrepresentable result undefined. The scanset
is a 256-bit automatic bitmap, 32 bytes of frame and nothing between calls.
`%n`, `%i`, `%p`, `%u`, `hh`, `ll`, `z`, `j` and `t` are added. `%X` and the
uppercase floating conversions follow their C17 lowercase semantics; the
documented `%D` and `%O` long-integer extensions remain available to Tcl and
historical callers.

This is deliberately not 499's answer to the same defect. 499 bounds the write
with a 513-byte buffer and a 256-byte class table, both automatic; this
removes the buffer instead. The rule is recorded as M13 in
`docs/research/STYLE-GUIDE.md`.

Floating conversion moves to `lib/libc/stdio/doscan_float.c` behind a weak
`__doscan_cvt`, mirroring `__doprnt_cvt` in `doprnt.c`, with `SCANF_FLOAT=yes`
in `share/mk/sys.mk` forcing the member. It stages significant digits in a
fixed buffer while a decimal exponent absorbs everything outside it, so input
of any length converts without the buffer growing with the input.
`MANT_DIGITS` and `EXP_LIMIT` each carry a `_Static_assert` tying them to
`DBL_DIG` and the decimal range of a double. This is the same separation
499 reaches with `pfcom.c` and its `fltused` symbol, expressed with the
mechanism this tree already uses rather than inline assembly.
The target `strtod` now carries the complete decimal power table through
`1e256`; the scanner gate links that tree source and covers finite `1e100`,
positive overflow and negative underflow instead of resolving host `strtod`.

### Measurements

Library members, `arm-none-eabi-size`, cross compiler 16.2.0 at `-Os`:

| Member | Before | After | Delta |
| --- | --- | --- | --- |
| `doscan.o` | text 1276, data 256 | text 1452, data 0 | text +176, data -256 |
| `ungetc.o` | text 58 | text 66 | text +8 |
| `doscan_float.o` | absent | text 812 | opt-in only |
| `strtod.o` | text 524 | text 552 | text +28, reached by float scanning only |

`_doscan`'s automatic frame, `-fstack-usage`: 128 bytes before, 120 after.
`__doscan_cvt` adds 120 bytes, only in a program declaring `SCANF_FLOAT`.

Whole programs, the tree's own `tools/bin/size` on the final a.out, over the
244 programs built both before and after. 42 changed:

| Delta (text+data+bss) | Programs | Cause |
| ---: | ---: | --- |
| -324 | 3 | lost `_sctab` and the otherwise-unreferenced `_ctype_`; scanner growth retained part of the saving |
| -304 | 5 | the same dependency removal with different final alignment |
| -64 | 21 | lost `_sctab`; another object still pulls `_ctype_` |
| -44 | 1 | the same retained `_ctype_` case with different final alignment |
| +8 | 10 | `ungetc` only, no scanner |
| +1344 | 1 | `trek` now links the float scanner and complete target `strtod` |
| +1360 | 1 | `primes` now links the float scanner and complete target `strtod` |

Sum over changed programs: **-1096 bytes**. The dependency groups are the
measured final-a.out results; object-size arithmetic does not substitute for
them. They also illustrate the dependency
condition the style guide's section 10 insists on: removing a table only pays
when nothing else in the program pulls it back.

Twelve of the changed programs are in the rp2040 manifest: `sysctl`,
`adminbox`, `utilbox`, `textbox`, `as`, `awk`, `cut`, `more`, `nl`, `resize`
and two `uudecode` paths. `distrib/rp2040/sdcard.img` is byte-identical in
size at 1012736, which is the fixed image geometry rather than a measurement
of slack.

A correction to an earlier draft of this work: `games/keen` was described as a
second board-reachable consumer of the overflow. It is not. Its two `sscanf`
calls sit inside `#ifdef HOSTBUILD` at `games/keen/keen.c:494` to `:625`, so
the board build never compiles them, and keen's data segment of 176 bytes is
smaller than `_sctab` alone, which is how the error was caught. `sysctl` is
the shipped path.

### The gate

`check-libc-scanf` in `tests/libc_contracts`, wired into `HOST_GATES` and
described in `sys/arch/rp2040/doc/TESTING.md`. It compiles the tree's scanner,
`strtod` and character-class table against the tree's headers, so the `FILE`
layout, numeric conversion and the `getc` and `ungetc` macros under test are
the target's, and runs at host width, at ILP32 and under the address sanitizer.
All three pass.

The sanitizer tier is not decoration. A staging overrun lands inside the
scanner's own frame, where a guard byte around the caller's destination sees
nothing, so it is the only tier that can observe the defect at all.

**Calibration.** The gate was built against the scanner it replaces and
required to fail, which it does, aborting under the sanitizer:

- first at `_innum`, writing 8 bytes through a pointer to a 4-byte
  `unsigned int` -- the `%X` defect, which this reviewer had predicted from
  reading but had not expected the gate to catch as a memory error;
- then, isolated in a single-case build, at `*np++ = c` in `_innum` with a
  512-digit run against an unwidthed `%ld` -- the staging overrun.

The replacement passes both. A gate that had only ever seen the new code would
have proved nothing about either.

### What this unit does not establish

Not run: any board execution. No claim here reached hardware. `%f` is
demonstrated by the host gate and by cross-compilation, not by a board run of
`primes` or `trek`. The `ungetc` residual case, a pushback of a character
different from the one read, is unfixed and bounded only by the note in the
source. The `0x` with no following hexadecimal digit consumes the `x`, which
departs from the longest-subject-sequence rule by one character and is the
documented cost of a single-character pushback.

## Queued campaigns

Each names its gate. None is started.

| Name | Scope | Gate that must exist first |
| --- | --- | --- |
| Torek FILE evaluation | Done and declined: `stdio-torek-evaluation.md`. The prototype on `eval/stdio-torek` builds and passes the stdio gates, and costs a median +534 bytes per program, +256 data in every program, and pushes `adminbox` to 23 packed blocks against its budget of 22. Two small mechanisms from it are queued below | The full build and the three stdio contract gates ran; the `adminbox` packed-root gate was the deciding measurement |
| One-byte pushback slot | Done: `_ub[1]` and `_IOUNGET` in `struct _iobuf`, the saved count parked in `_bufsiz`, which a string stream never refills from; `FILE` 20 to 24 bytes. `filbuf.c`, `flsbuf.c`, `findiop.c`, `ungetc.c` and `exit.c` are C17 with prototypes in the same change, and `_f_morefiles` now fails whole rather than leaving `_smallbuf` short of the descriptor table | `check-libc-scanf` carries the differing-byte pushback over a literal at every tier; the V7 `ungetc` faults on it |
| `r+` mode discipline | Flush before read and discard the read buffer before write on one stream, as 499's `__srefill` and `__swsetup` do | A host contract that writes, reads back and writes again on one `r+` stream |
| `vfprintf` conformance | 499's `vfprintf` against this tree's `_doprnt`: `%a`, the `'` and `z`/`j`/`t` modifiers, and the return-value contract | Extend `check-libc-printf` with the directives `_doprnt` lacks, calibrated against the current formatter |
| Patch 460 ANSI sweep | The 17 shared files of the upstream ANSI groundwork, as a source of prototypes rather than a patch application | `check-warning-policy-host` at `full` for each directory touched |
| getty console fixes | Applied: 480's NoParity (`np`) gettytab flag and `PASS8`, 484's 8-bit local flags set through `TIOCLSET` so login keeps them, 487's `ec` editing entry and the flag-table renumbering the new entries force, 493's gettytab.h revision; the kernel's `tty.c` implements `TIOCLSET` and `PASS8`. Five upstream hunks that only bumped SCCS identifiers were dropped, and one that removed `ctty[]` and a `ttyname()` declaration was not taken because this tree still uses both | `check-renode` is the console gate and it fails on `main` before this change, at `/etc/rc`'s first line; `renode-login-regression.md` records the runs. Build and size evidence only: getty text 16140 to 16232, data 2020 to 2028 |
| `srandom` argument fix | Patch 452 across 16 shared files, mostly games | `games/keen` uniqueness and the existing per-game host suites |
| Networking resolver fixes | Patches 432, 487 and 489 against `lib/libc/net` | None. This port has no network; inventory only |

## Reference

| Subject | Authority |
| --- | --- |
| Mapping method and thresholds | `tools/analysis/bsd211_patch_scope.py` |
| Patch 499 announcement, CHANGES, `stdio.h` and `_fwalk` | `http://www.2bsd.com/2.11BSD/499` |
| Scanner contract and calibration | `tests/libc_contracts/scanf_contract_test.c`, `sys/arch/rp2040/doc/TESTING.md` |
| Float opt-in mechanism | `share/mk/sys.mk`, `lib/libc/stdio/doprnt.c`, `lib/libc/stdio/doscan.c` |
| Style rules this unit exercises | `docs/research/STYLE-GUIDE.md`, sections 4, 8 and 11 |
| Upstream defect report | `docs/research/211bsd-fwalk-report.md` |
