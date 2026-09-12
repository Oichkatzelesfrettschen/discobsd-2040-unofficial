# RP2040 correctness-audit response

This records what the read-only audit ledger
(`/home/eirikr/worktrees/discobsd-tmp/FINDINGS.md`, SHA-256
`7ae8cf27fe2b7c0a847580e3b84476b865041ae84da6b49dc509dbb6a7d61130`) found and
what this branch changed. The audit's own gate is lexicographic: correctness
and ABI violations outrank every optimization, and exact linked evidence
outranks source projections. Each item below states the fix, the evidence, and
the falsifier or deferral reason. Every fix was verified against source and,
where it changes machine code, against the relinked image; on-device
revalidation of the account, float, and divider behavior remains a separately
authorized attended step, because a concurrent flash during the audit broke
the temporal tie between the earlier hardware observations and the bytes now on
the board.

## Correctness fixed this pass

### SVC exception priority no longer underflows (ranked #4)

`intr.h`'s `IPLTOREG` formed the priority byte through `NVIC_PRIO_LEVELS`,
whose `1U` shift makes the product unsigned. `IPL_SVCALL` is `IPL_TOP`, one
above `IPL_HIGH`, so `IPL_HIGH - ipl` is -1, which the unsigned conversion and
final mask turned into `0x80` rather than `0x00` -- SVC ended up less urgent
than SysTick, contradicting the highest-priority-SVC contract. The macro now
maps every level at or above `IPL_HIGH` to the most urgent byte `0` and keeps
the negative difference out of the unsigned arithmetic. The relinked PICO image
confirms it: `startup()` writes SHPR3 PendSV `0xc0` and clears the SHPR2 byte
for SVC to `0x00` (`lsls #8; lsrs #8`, no OR), where it previously stored
`0x80`. Falsifier: any IPL maps outside the range, or the linked SHPR store for
SVC is not `0x00`.

### copyin/copyout reject zero length before end-address arithmetic (ranked #5)

Both computed `baduaddr(ptr + nbytes - 1)` with no zero guard, so a valid
zero-length transfer evaluated `ptr - 1` and could be rejected or wrap. Both
now return success for `nbytes == 0` before the arithmetic (`machdep.c`).
Falsifier: a zero-length copy through either seam still touches `ptr - 1`.

### bzero returns before its alignment prefix at zero length (ranked #6)

The alignment prefix loop ran before any length check: `bzero(unaligned, 0)`
wrote one byte, then `--nbytes` wrapped 0 to `SIZE_MAX` and the word loops
scribbled the address space -- a ~4 GB write, not the "one or more bytes" the
audit estimated. `bzero` now returns immediately for `nbytes == 0`
(`machdep.c`). Falsifier: a canary adjacent to an unaligned zero-length
destination changes.

### USB IRQ diagnostic is type-correct (ranked #9)

`usb.c` passed the signed `int` macro `USBCTRL_IRQ` to `%u`. The format is now
`%d`, matching the argument type. Falsifier: the promoted argument at the call
is not `int`, or the format checker rejects it.

### Operator-login and web-listener documentation matches the board (ranked #10)

Six surfaces advertised blank-password root login, which `login.c` refuses on
the console, and one described the web listener as binding every interface,
which the parser no longer does. `README.md`, `USER-ACCESS.md` (login line and
web-bind paragraph), `distrib/rp2040/host/discobsd-web` (banner, "root
console" text, and the `--token FILE` help that the parser reads as a literal
secret, now `--token SECRET`), and `distrib/rp2040/host/discobsd-term` (two
lines) now state operator login followed by wheel `su`, loopback-by-default
binding, and a literal token. The two `login: root` transcripts elsewhere in
`README.md` are STM32 (F412GDISCO) and PIC32 (MAX32) boots, whose account model
allows root login, and are unchanged. The byte-identical deploy mirrors under
`rpi/pico-host/` are outside this repository and need re-syncing from the
corrected `distrib/rp2040/host/` copies at deploy time.

### CoreMark reports a microsecond-accurate rate (ranked #11 / gate 5)

`core_main.c` is unmodifiable, and with `HAS_FLOAT=0` its report divides the
iteration count by the whole-second `time_in_secs()`, so `Iterations/Sec`
truncates (2000 / 8 = 250 for an 8.254 s, 242.3/s run) and its canonical
`CoreMark 1.0` score line, guarded by `HAS_FLOAT`, is omitted. The port's
`portable_fini` (a modifiable file) now recovers the iteration count from the
enclosing `core_results` and recomputes the rate at microsecond resolution,
printing `Iterations/Sec` and a `CoreMark 1.0` line in fixed point. The product
uses 64-bit intermediates but only `%lu` (32-bit) reaches `printf`, so no `%f`
or `%llu` formatter runs -- the `%f` path is what overflowed the process stack.
Host validation: a 200000-iteration run printed core_main's truncated
`40000` beside the port's accurate `35469.45` (= 200000 / 5.638655), CRCs
matching the board. The false "exact in integer form" comment in
`core_portme.h` is corrected. Falsifier: the port's rate differs from
`iterations * 1e6 / elapsed_us`, or a float formatter appears in the image.

### CoreMark source-integrity check passes (adjacent to gate 5)

`bmake check` failed on `coremark.h` before this branch, on `main` too. The
cause is upstream: eembc/coremark ships a `coremark.md5` whose `coremark.h`
entry (`8ca974c0...`) is stale against its own committed `coremark.h`
(`b0ec69b6...`). The five `.c` files here are byte-identical to upstream `main`
(direct `cmp`), and `coremark.h` matches upstream `main` too; the vendoring
faithfully copied both the file and the stale hash. `coremark.md5` is
regenerated from the pristine bytes, so all six now verify. Provenance: the six
core files are byte-for-byte eembc/coremark `main` at commit
`1f483d5b8316753a742cbf5590caf5bd0a4e4777`. Falsifier: any core file differs
from that upstream tree, or `bmake check` fails.

### Native AEABI float seam (gate 4, first half)

The board carries no libgcc, `smlrc` emits the bare `__aeabi_fadd/fsub/fmul/
fdiv` names (`cgthumb.c`), and `rom_float.o` exports only `__wrap___aeabi_*`
with dangling `__real___aeabi_d*` references. The board's native `ld` has no
`--wrap`, so it can never select the wrappers: an on-board float link resolves
`__aeabi_fadd`, which the board libc.a never provided, before or after the
shim. This is a pre-existing reachability gap, not a regression -- nothing
on-board references `__wrap___aeabi_*`, so `rom_float.o` is never pulled from
the archive and its `__real` references never become undefined (verified by
`nm` over the built board archive). `rom_float` is therefore inert dead weight
in the board libc, so it is removed from `boardlibc-members` and
`mkboardlibc.py`; the rebuilt board archive returns to the pristine 36412 bytes
/ 89 members with no `__wrap`/`__real`/`__aeabi_f` symbols. The shipped
userland is unaffected: it cross-links against the host ELF `lib/libc.a`, where
`--wrap` (share/mk/sys.mk, host links only) routes the AEABI arithmetic through
`rom_float.o`. On-board float via the ROM remains possible through a separate
board-only object that exports the bare `__aeabi_*` names, plus a V1
double-fallback source; that needs on-device qualification and is deferred
(gate 25).

### PICO_UART divider restore ordering (gate 1)

The shared `locore.S` restores the SIO divider through `env` (r10) before the
register restore reloads r10; the committed PICO_UART binary predates that fix
and still reloads r10 first. A rebuild from current source produces the correct
order: in the relinked PICO_UART, `mov r3, sl; adds r3, #40` at `0x10000ec0`
runs before `mov sl, r6` at `0x10000eec`. The reflash flow rebuilds the kernel
from source rather than flashing the committed artifact, so parity holds once
built; the stale tracked binary is a hygiene matter (below).

### etc distribution refreshes the account database for every image (user flag)

`${FSIMG}` stages files from `${DESTDIR}` through the manifest, but
`DESTDIR/etc/passwd`, `etc/shadow` and `etc/group` reach `DESTDIR/etc` only
through `etc`'s own `distribution` target, which ran only inside the top-level
`distribution` target. The `flash` -> `fs` -> `${FSIMG}` path never triggered
it, so `bmake flash` imaged whatever stale `DESTDIR/etc` a prior run left --
the account regression seen earlier. `${FSIMG}` now depends on a phony
`etc-distribution` target that runs the same install, so every image path
refreshes the account files. Verified: a `DESTDIR` seeded with a stale
`operator:*` locked entry became the source `operator:139` after the step.
Falsifier: an image built through `fs`/`flash` carries an `etc/passwd` older
than the source.

Ordering: `${FSIMG}` does not depend on `build`, and `etc distribution`
reads `DESTDIR/usr/share/misc/termcap`, so `fs`/`flash` now require a prior
`bmake build`; on a bare tree the new prerequisite fails inside the etc
install rather than at the manifest step. `bmake -n fs` confirms the etc
distribution runs before the manifest `cat` and `fsutil`.

### fptest is a tree regression test with a bit-exact oracle (user flag)

`tests/rp2040/fptest/` replaces the scratchpad harness. It compares the wrapped
ROM float result bits against host-computed IEEE-754 round-to-nearest-even
expected bits -- an oracle independent of the code under test, unlike an
absolute-error check whose own subtraction runs the wrapped path. Bit
comparison also distinguishes signed zero. Operands pass through volatile
locals so the compiler emits run-time `__aeabi` calls rather than folding the
table. It builds clean through the cross toolchain and `--wrap` seam (14332-byte
a.out) and is not in the shipped manifest. Deferred within the test: subnormal,
NaN, infinity, overflow, and halfway-rounding cases, whose expected bits follow
the ROM's flush/map contract and need on-device characterization before they
can be an oracle.

## Accepted limitation, not a code change

### tsleep panic path (ranked #8)

`kern_synch.c` is machine-independent. On pic32 and stm32 the panic path's
`splnet()` after `splhigh()` genuinely lowers to a network IPL and opens an
interrupt window; the path is a no-op only on rp2040, whose PRIMASK masks
everything or nothing, so no partial `spl` window exists. Editing the shared
file to suit one port would regress the others. This is an accepted rp2040
architectural limitation: on a panic the "give interrupts a chance" comment
cannot be honored, which is acceptable on a path that is about to reset.

## Deferred, with the reason each stays open

These are the audit's remaining correctness items. Each needs an attended
hardware session or a codegen/exception-entry change whose falsifier is a
differential-execution matrix, not static reasoning, so each belongs on its own
branch behind a byte-range or differential oracle rather than mixed into this
pass.

- Signal frame aliases the exception frame (ranked #1). A genuine
  memory-corruption bug; a wrong signal-frame change breaks signal delivery in
  a way only an attended `sigreturn` test on hardware reveals.
- Smaller-C AAPCS call-site misalignment (ranked #2) and exception entry in
  ordinary C (ranked #3). Codegen and exception-entry changes; falsifiers are
  compile-matrix disassembly and differential execution.
- RP2040-E15 Bulk-IN frame guard (ranked #7). Needs affected B2/VL805 hardware
  to prove the time-window guard.
- Kernel-owned divider transactions across IRQ, signal, fork, exec, exit, and
  DIRTY/READY transitions (gate 2), and a forced-collision `divrace` oracle
  with a negative control (gate 3). The context-switch checkpoint has landed and
  is verified under load; the remaining paths and a collision oracle that
  observes a switch inside the ROM divide window are separate work.
- Native on-board float via ROM (gate 25), the duplicate-BSS-clear (ranked #12),
  the `resume` exchange loop (#13), and every optimization item #14-30, which
  the audit gates behind exact relinked measurement.

## Hygiene notes

- The tracked kernel binaries under `sys/arch/rp2040/compile/*/unix*` are stale
  build outputs: the divider commit `0a09de3f` changed source without
  regenerating them, so both PICO and PICO_UART committed images predate the
  fix. The reflash flow rebuilds from source, so this was a tracking-hygiene
  issue, not a flashed-kernel one. Decision taken: stop tracking the generated
  compile outputs rather than commit fresh binaries. The linked images, version
  stamps, dependency files, boot2 blobs and machine/sys symlinks are removed
  from tracking and matched by `.gitignore`; `make clean`/`clean-all` remove
  them; the config scaffolding (Config, Makefile, ioconf.c, swapunix.c) stays
  tracked because the default build consumes the committed compile dir without
  rerunning config. A from-clean worktree builds the PICO kernel from that
  scaffolding alone (verified). This closes gate 1: the stale broken kernel
  binary no longer ships in git.
- The `rpi/pico-host/` copies of `discobsd-web`/`discobsd-term` are outside this
  repository; re-sync them from the corrected `distrib/rp2040/host/` copies.

## Follow-up pass: hygiene, constrained-C, and the man-page decision

### Generated kernel build outputs are no longer tracked

See the hygiene note above; this is the implementation of the gate-1 decision.

### Deploy mirrors resynced

The `rpi/pico-host/` copies of `discobsd-web`/`discobsd-term` are resynced from
the corrected `distrib/rp2040/host/` copies and are byte-identical again. A
running web/terminal console holds the old text in memory until restarted.

### NSTATIC 20 -> 8 (ranked #21)

`lib/libc/stdio/findiop.c` reserved 20 static FILE slots; eight keep stdin,
stdout, stderr and five more before `_findiop` falls to the dynamic
`_f_morefiles` path. `findiop.o`'s `_iob` drops 400 -> 160 bytes and `sbuf`
20 -> 8. Measured at the image level, not the source-object sum: the coremark
a.out shrank 17496 -> 17256, exactly 240 initialized data bytes; the 12-byte
bss drop does not ship in an a.out. Across the audit's 21 proven payers that is
about 5040 image bytes before filesystem block packing, which is where any
freed root block would show. Gate remaining: an on-device test that opens nine
concurrent streams and exercises the `_f_morefiles` allocation and failure
paths; the code path is unchanged from the >20-stream case, only its threshold
moved. Printf-float root removal (ranked #22) is a separate pass -- twenty
relinks with two unresolved cases (`picoc`, `tclsh`) -- and is not bundled
here so this measurement stays clean.

### man.c descriptor bug fixed; the archive is a decision for the user (ranked #2)

`usr.bin/man/man.c` `cat()` guarded the descriptor with `if (!(fd = open(...)))`,
true only for descriptor 0 and false for the -1 failure; it now tests `== -1`.
The compressed-man-page archive itself is not started. The audit falsified the
10240-byte premise: the seven pages compress to 42728 bytes with per-stream
integrity metadata, the image has 175 free blocks against 42 data + 1 indirect
+ 1 inode, and the feature also needs a userland decoder (the kernel's is not a
stable ABI), `man` folded into utilbox, a rewritten `cc.0` that documents this
board's driver rather than PCC, and attended on-device pager testing. That is a
feature requiring a board session, not a cleanup item; it stays a scoping
decision: 42728 bytes and roughly 132 blocks of headroom.

## Corpus regenerator for fptest

The fptest expected values come from this host helper (compile, run, paste the
output into `corpus_single.h`/`corpus_double.h`); it emits IEEE-754
round-to-nearest-even results, which the bootrom reproduces exactly for finite
normal operands:

    /* single/double: {a_bits, b_bits, OP_*, expected_bits} via memcpy punning
       and the host's own +,-,*,/ on float/double. */
