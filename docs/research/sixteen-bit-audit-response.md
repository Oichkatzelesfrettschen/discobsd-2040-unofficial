# 16-bit assumption audit: dispositions

Drafted with Claude Code. This note answers a submitted audit of 16-bit
assumptions in the RP2040 port. The audit text itself is not in the tree;
each item below is restated with the evidence that decides it. Source
revisions: the audit was checked against `6e4c3a4`; the repairs land in
the five commits after it, `1a9af9e` through `ba39f87`.

The audit's thesis holds and governs every disposition: Thumb-1 encodes
instructions in 16 bits, but the target runs an ILP32 AAPCS ABI, so a
narrow C object is justified by a bounded external format or a measured
resident saving, never by the instruction width. On Cortex-M0+ the narrow
type usually costs code: the libtcl repair below removed 12 bytes of text
by widening extents, because `unsigned short` arithmetic needs `uxth`
truncations that `int` does not.

## Already repaired before the audit was checked

`ce33939` (2026-10-04) carries the four kernel findings and their tests.
`check-storage-correctness` links the kernel sources on the host and
calibrates ten source mutations; `tests/kernel/cred_test.c` and
`tests/kernel/time_io_test.c` hold the boundary cases.

| Audit item | Current source | Test |
| --- | --- | --- |
| `adjtime()` `adjust < 0x8000` | `sys/kern/kern_time.c` compares `adjust < -0x8000` in `int64_t`, normalizes backward steps and refuses an unrepresentable step | `time_io_test.c`: -32769, -32768, 0, 32767, 32768 |
| `setpgrp()` and `TIOCSPGRP` narrow `int` to `short` | `PGRP_VALID()` in `sys/sys/proc.h` rejects values outside 0..`SHRT_MAX` before lookup, authorization or store | `cred_test.c` `compact_boundaries`: -1, 32768, 65536, 65553, `INT_MAX` refused; 0, 29999, 30000, 32767 accepted |
| `NOGROUP` 65535 reserves a valid GID | `NOGROUP` is `((gid_t)-1)`; `setgroups`, `setgid` and `setegid` refuse it; `u_groups` is documented as NOGROUP-terminated | GIDs 65535 and 65536 round-trip; the sentinel is refused without a partial update |
| SVC immediate read through `int *` | `sys/arch/rp2040/rp2040/syscall.c` reads `*(const unsigned char *)u.u_code` | no dedicated test; every syscall in a Renode or board boot executes it |
| `tsleep()` timeout above `INT_MAX` | `sys/kern/kern_synch.c` returns `EINVAL` before queue insertion; the comment states the `INT_MAX` bound | none; source inspection only, since no host gate links `kern_synch.c` |

`p_uid` is already `uid_t` in `struct proc`, so the audit's `ps` row
concerns only the user-side record.

## Repaired here

| Commit | Defect | Mechanism | Gate |
| --- | --- | --- | --- |
| `1a9af9e` | `Var.valueLength/valueSpace`, `Interp.appendAvl/appendUsed`, `ParseValue.expandProc`'s `needed`, `tclexpr.c` string `length/space` in `unsigned short` | a value, result or word past 65535 bytes stored a wrapped capacity; the next growth sized from it and copied the true string: heap or stack overflow | `check-libtcl-contracts`: var-, append-, parse-, expr-extent |
| `1a9af9e` | regexp first pass counted the program in `unsigned short`; no bound against `NEXT()`'s two-byte offsets | a ~65.5 KB literal allocated short, emitted past its end; `regexp_size()` now refuses a program past 0xffff | regexp-program |
| `1a9af9e` | STAR/PLUS backtrack count `unsigned short` | a STAR backtrack that reached zero without a match wrapped the count to 65535 and read 65535 bytes past the operand (`xa*b` against `xaac`), at any subject length; a run past 65535 characters also lost its count | regexp-backtrack |
| `1a9af9e` | `end` = 32767 (`lreplace`), 30000 (`lrange`) | artificial list ceiling; now `INT_MAX`, which both element walks stop short of | list-end |
| `fa57b80` | `Tcl_Eval` skipped `':'` instead of `';'` | every `cmd; cmd` script looped forever | command-separator (alarm) |
| `a0d4a15` | `regexp`/`regsub` tested `regexp_execute()`'s 1-on-match as failure | `regexp` answered inverted; `regsub` on a miss wrote through a pointer built from unwritten `startp[0]` | regexp-result |
| `10b1ab8` | `psout.o_uid` `short` | UIDs 32768-65535 printed near 2^32 and never matched a passwd name | `_Static_assert` in `bin/ps/ps.c`, calibrated by restoring `short` |
| `ba39f87` | `CTLTYPE_INT` "16-bit", `sigprocmask` "16-bit error indication", `stat.h` "u_short flags word" | comments describing PDP-11 limits | comment-only |

The command-separator and regexp-result defects are outside the audit's
width class; the gate exposed them while calibrating, and the width cases
could not be exercised through `regexp` or multi-command scripts without
them. All three libtcl defect classes predate this fork: `git log -S`
places them in the 2022-10-19 import `73fb1cc2`, so upstream DiscoBSD
carries them. No upstream report has been sent.

Evidence classes for the libtcl rows: host x86_64 with ASan and UBSan
(library source, native LP64 width), and a Cortex-M0+ compile of
`lib/libtcl` with no compiler warnings. Neither is a board run, and the
host gate does not run at ILP32. On the target, `sizeof(Var)` grows from
24 to 28 bytes and `sizeof(Interp)` from 448 to 452; library text shrinks
from 49286 to 49274 bytes.

## Retained narrow, as the audit recommended

Boot-ROM 16-bit table pointers, Thumb instruction and SVC encodings,
USB descriptor fields, ELF halfwords, MMIO and peripheral fields,
`COMPACT_SWAPMAP`, SwapRAM 16-bit offsets, compact inode fields with their
`_Static_assert` and `EIO` path, exec spool lengths under `ARG_MAX` 5120,
8.8 load average, `statfs` block sizes, directory and inode disk formats,
`FILE._file` and `_flag`, profiling buckets, a.out and archive fields, and
`RAND_MAX` 0x7fff for the 15-bit compatibility `rand()`.

Within libtcl, the regexp program-format widths (`NEXT()` offsets,
`mustlen`, `EXACTLY` lengths, `regtail` offsets) stay 16-bit: the
program bound now caps every one of them below 65536.

## Deferred, with the reason and the validation path

| Item | Why it stays | What would change that |
| --- | --- | --- |
| `vmrate` `u_short` counters | `UCB_METER` is defined in no RP2040 configuration, so the structure is compiled out of every shipped kernel | enabling `UCB_METER`; then measure the five-second event peak against 65535 |
| `rom_func_lookup()` spelling | it already reads both ROM tables at 16-bit width through `volatile u_short *`; `uint16_t`/`uintptr_t` would be spelling only | a C17 conversion of `flash.c` |
| Smaller C `MaxParams` 32767 and similar | the Thumb back end sets `SizeOfWord` 4; the constants are sentinels, not ABI widths | modernizing `usr.bin/smlrc` |
| libtcl history structures | no `tclHistory.c` is built; `events` is never allocated | adding the history module |
| `regexp_substitute()` `len` | `regsub.c`'s function has no caller and `regexp.h` is not installed | a caller |
| `Var.upvarUses` `unsigned short` | 65536 references at 28 bytes each exceed the 144 KB process window | a larger window |
| `Interp.patLengths[]` | a wrapped length only misses the four-entry pattern cache | none needed |
| libtcl `malloc()` results unchecked | pervasive in Tcl 6; exhaustion dereferences NULL rather than failing the command | a library-wide allocation policy |
| `games/arithmetic` `hmul()` "16-bit" | the documented precondition holds for its callers | none needed |

`bmake MACHINE=rp2040 build` builds `usr.bin/tclsh` against the repaired
library, but no RP2040 profile ships it: `distrib/base/mi` lists
`/usr/bin/tclsh` and `distrib/rp2040/mi.rp2040`, from which every RP2040
manifest is composed, does not. The libtcl repairs therefore reach the
built binary and any image that adds it, not the current root. A board
run of tclsh on values past 65535 bytes, which the 144 KB window permits
only for values near that size, remains unmeasured.
