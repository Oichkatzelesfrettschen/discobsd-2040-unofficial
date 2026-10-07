# 16-bit assumption audit: dispositions

Drafted with Claude Code. This note answers a submitted audit of 16-bit
assumptions in the RP2040 port. The audit text itself is not in the tree;
each item below is restated with the evidence that decides it. Source
revisions: the audit was checked against `6e4c3a4`; the repairs land in
the commits after it, `7c726e8` through this note's own update.

The audit's thesis holds and governs every disposition: Thumb-1 encodes
instructions in 16 bits, but the target runs an ILP32 AAPCS ABI, so a
narrow C object is justified by a bounded external format or a measured
resident saving, never by the instruction width. On Cortex-M0+ the narrow
type usually costs code: widening the libtcl extents in `7c726e8` removed
12 bytes of text, because `unsigned short` arithmetic needs `uxth`
truncations that `int` does not.

## Already repaired before the audit was checked

`ce33939` (2026-10-04) carries the four kernel findings and their tests.
`check-storage-correctness` links the kernel sources on the host and
calibrates source mutations; `tests/kernel/cred_test.c`,
`tests/kernel/time_io_test.c` and `tests/kernel/synch_meter_test.c` hold
the boundary cases.

| Audit item | Current source | Test |
| --- | --- | --- |
| `adjtime()` `adjust < 0x8000` | `sys/kern/kern_time.c` compares `adjust < -0x8000` in `int64_t`, normalizes backward steps and refuses an unrepresentable step | `time_io_test.c`: -32769, -32768, 0, 32767, 32768 |
| `setpgrp()` and `TIOCSPGRP` narrow `int` to `short` | `PGRP_VALID()` in `sys/sys/proc.h` rejects values outside 0..`SHRT_MAX` before lookup, authorization or store | `cred_test.c` `compact_boundaries`: -1, 32768, 65536, 65553, `INT_MAX` refused; 0, 29999, 30000, 32767 accepted |
| `NOGROUP` 65535 reserves a valid GID | `NOGROUP` is `((gid_t)-1)`; `setgroups`, `setgid` and `setegid` refuse it; `u_groups` is documented as NOGROUP-terminated | GIDs 65535 and 65536 round-trip; the sentinel is refused without a partial update |
| SVC immediate read through `int *` | `sys/arch/rp2040/rp2040/syscall.c` reads `*(const unsigned char *)u.u_code` | no dedicated test; every syscall in a Renode or board boot executes it |
| `tsleep()` timeout above `INT_MAX` | `sys/kern/kern_synch.c` returns `EINVAL` before queue insertion | `synch_meter_test.c` since `3d8d183`: `INT_MAX` reaches `timeout()` whole; `INT_MAX + 1` and `UINT_MAX` are refused with nothing queued |

`p_uid` is already `uid_t` in `struct proc`, so the audit's `ps` row
concerns only the user-side record.

## Repaired here

| Commit | Defect | Mechanism | Gate |
| --- | --- | --- | --- |
| `7c726e8` | `Var.valueLength/valueSpace`, `Interp.appendAvl/appendUsed`, `ParseValue.expandProc`'s `needed`, `tclexpr.c` string `length/space` in `unsigned short` | a value, result or word past 65535 bytes stored a wrapped capacity; the next growth sized from it and copied the true string: heap or stack overflow | `check-libtcl-contracts`: var-, append-, parse-, expr-extent |
| `7c726e8` | regexp first pass counted the program in `unsigned short`; no bound against `NEXT()`'s two-byte offsets | a ~65.5 KB literal allocated short, emitted past its end; `regexp_size()` now refuses a program past 0xffff | regexp-program |
| `7c726e8` | STAR/PLUS backtrack count `unsigned short` | a STAR backtrack that reached zero without a match wrapped the count to 65535 and read 65535 bytes past the operand (`xa*b` against `xaac`), at any subject length; a run past 65535 characters also lost its count | regexp-backtrack |
| `7c726e8` | `end` = 32767 (`lreplace`), 30000 (`lrange`) | artificial list ceiling; now `INT_MAX`, which both element walks stop short of | list-end |
| `2aaf684` | `Tcl_Eval` skipped `':'` instead of `';'` | every `cmd; cmd` script looped forever | command-separator (alarm) |
| `a2581c5` | `regexp`/`regsub` tested `regexp_execute()`'s 1-on-match as failure | `regexp` answered inverted; `regsub` on a miss wrote through a pointer built from unwritten `startp[0]` | regexp-result |
| `8d944d9` | `psout.o_uid` `short` | UIDs 32768-65535 printed near 2^32 and never matched a passwd name | `_Static_assert` in `bin/ps/ps.c`, calibrated by restoring `short` |
| `38e08d5` | `CTLTYPE_INT` "16-bit", `sigprocmask` "16-bit error indication", `stat.h` "u_short flags word" | comments describing PDP-11 limits | comment-only |
| `777d363` | `Interp.patLengths` `unsigned short` | an unused cache slot's -1 became 65535, so a 65535-byte pattern was compared with that slot's NULL pattern before compilation refused it | pattern-cache |
| `777d363` | `Var.upvarUses` `unsigned short` | 65536 `upvar` references wrapped the count to 0, and unsetting the target freed a variable they all still pointed at | upvar-count (heap use-after-free when narrowed) |
| `777d363` | `regexp_substitute()` `len` `unsigned short` | a submatch past 65535 characters was copied short | regexp-substitute |
| `39b5083` | `regexp -nocase` freed its lower-cased copy before computing offsets from it | arithmetic on a freed pointer (C17 6.2.4p2); compilers still return the intended offsets | regexp-result covers the path; no sanitizer discriminates the old order |
| `750b96e` | `rom_func_lookup()` widened 16-bit ROM addresses through `u_int` with the width unstated | a change of `u_int` or `u_short` width would truncate a ROM address silently | `_Static_assert`, calibrated; PICO and PICO_UART `unix.bin` differ only in the 8-byte version string |
| `35bdc36` | `struct vmrate` counters `u_short` | `machparam.h` defines `UCB_METER` by default, so every kernel counts; more than 65535 events in one `vmmeter()` second lost multiples of 65536 from `sum` and `rate` | `check-storage-correctness` synch suite: 70000 counted system calls; the narrowed struct, reaching the kernel objects since `32ad827`, fails |
| `07fb61c` | `vmstat` printed the `u_int` rates through `%d` | each `rate.x / nintv` is `unsigned long`; it was `long`, not `int`, before | none: the tree's `printf` has no format attribute, so `-Wformat` checks neither form |
| `bc6cda7` | `regsub` loop, reachable once `a2581c5` fixed the result test | an empty `-all` match looped forever; a match ending at offset 0 answered 0; the `-nocase` tail came out lower-cased; `^` matched at every `-all` resumption | regsub-cases, three controls (alarm, `Axc`, `bbb`) |
| `bc6cda7` | STAR backtracking formed `save - 1` before its loop test failed | a pointer before the subject when the run starts at its first byte | regexp-backtrack; `a*ab` against 70000 a's and a b now requires a full backtrack |

The command-separator, regexp-result, regsub and `-nocase` defects are
outside the audit's width class; the gate and a review of the branch
exposed them, and the width cases could not be exercised through
`regexp`, `regsub` or multi-command scripts without them. All of the
libtcl defects predate this fork: `git log -S` places the separator,
the result test and the backtrack count in the 2022-10-19 import
`73fb1cc2`, so upstream DiscoBSD carries them. No upstream report has
been sent.

Evidence classes for the libtcl rows: host x86_64 with ASan and UBSan,
under both cc 13.3.0 and clang 18.1.3 (library source, native LP64
width), and a Cortex-M0+ compile of `lib/libtcl` with no compiler
warnings. Neither is a board run, and the host gate does not run at
ILP32. The library is built without UBSan's bounds group: `Var` keeps
its value in a four-byte trailing union member that `NewVar()` sizes by
allocation, which clang's check reports and ASan bounds correctly.

Target cost, arm-none-eabi-gcc 13.2.1: `sizeof(Var)` 24 -> 28 bytes
(the reference count fills existing padding), `sizeof(Interp)` 448 ->
460, `lib/libtcl` text 49286 -> 49376 bytes. `struct vmrate` doubles to
40 bytes for `cnt` and `rate`; PICO text -8 bytes and bss +32, PICO_UART
text -8 and bss +64.

## Retained narrow, as the audit recommended

Boot-ROM 16-bit table entries, Thumb instruction and SVC encodings,
USB descriptor fields, ELF halfwords, MMIO and peripheral fields,
`COMPACT_SWAPMAP`, SwapRAM 16-bit offsets, compact inode fields with their
`_Static_assert` and `EIO` path, exec spool lengths under `ARG_MAX` 5120,
8.8 load average, `statfs` block sizes, directory and inode disk formats,
`FILE._file` and `_flag`, profiling buckets, a.out and archive fields, and
`RAND_MAX` 0x7fff for the 15-bit compatibility `rand()`.

Within libtcl, the regexp program-format widths (`NEXT()` offsets,
`mustlen`, `EXACTLY` lengths, `regtail` offsets) stay 16-bit: the
program bound caps every one of them below 65536.

## Deferred, with the reason and the validation path

| Item | Why it stays | What would change that |
| --- | --- | --- |
| Smaller C `MaxParams` 32767 and similar | the Thumb back end sets `SizeOfWord` 4; the constants are sentinels, not ABI widths | modernizing `usr.bin/smlrc` |
| libtcl history structures | no `tclHistory.c` is built; `events` is never allocated | adding the history module |
| libtcl `malloc()` results unchecked | pervasive in Tcl 6; exhaustion dereferences NULL rather than failing the command | a library-wide allocation policy |
| `vmstat` `sum` totals printed with `%d` | `long` through `%d`, unchanged by this work and the same size on the target | a format pass over `vmstat` |
| `games/arithmetic` `hmul()` "16-bit" | the documented precondition holds for its callers | none needed |

`bmake MACHINE=rp2040 build` builds `usr.bin/tclsh` against the repaired
library, but no RP2040 profile ships it: `distrib/base/mi` lists
`/usr/bin/tclsh` and `distrib/rp2040/mi.rp2040`, from which every RP2040
manifest is composed, does not. The libtcl repairs therefore reach the
built binary and any image that adds it, not the current root. Not
measured: tclsh on the board, and the board's per-second system call
rate that decides how often the former `vmrate` width wrapped.
