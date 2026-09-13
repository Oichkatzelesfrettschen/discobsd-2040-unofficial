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

### Native AEABI ROM float provider

The board carries no libgcc and its native linker implements no `--wrap` seam.
The RP2040 libc therefore exports the bare AEABI and compiler-runtime names
from 48 fine-grained assembly members. A complete runtime link can resolve 92
public names without libgcc; the member partition preserves all 54 names from
the former compiler-rt subset and adds the double, 64-bit conversion, and
AEABI comparison surfaces. Each executable extracts only the members reached
from its unresolved symbols.

The common resolver validates the public Boot ROM magic, compatibility byte,
version, table bounds, Thumb targets, and cache-cell address before publishing
one aligned target word. Boot ROM V1 supports the single-precision entries.
Double-precision entry resolution requires Boot ROM V2 or V3 and exits with
status 70 when the requested interface is unavailable. The verified B2 board
reports Boot ROM V3.

### Divider ownership

User Thread mode runs with `CONTROL[0]=1`, so a user helper cannot mask
interrupts. Direct ROM division instead relies on the bounded single-core
kernel invariant: healthy user instructions do not switch processes, and the
IRQ, NMI, callout, and kernel graph contains no SIO-divider consumer. Source
and linked-image gates enforce that absence. The obsolete four-word divider
checkpoint is removed from each `label_t`, which restores 48 bytes of stack
headroom per resident u-area and removes the save/restore work from every
kernel `setjmp` and `longjmp`.

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

`tests/rp2040/fptest/` replaces the scratchpad harness. It compares the native
ROM float result bits against host-computed IEEE-754 round-to-nearest-even
expected bits -- an oracle independent of the code under test, unlike an
absolute-error check whose own subtraction runs the wrapped path. Bit
comparison also distinguishes signed zero. Operands pass through volatile
locals so the compiler emits run-time `__aeabi` calls rather than folding the
table. It builds through the cross toolchain against the native AEABI provider
and is not in the shipped manifest. Deferred within the test: subnormal,
NaN, infinity, overflow, and halfway-rounding cases, whose expected bits follow
the ROM's flush/map contract and need on-device characterization before they
can be an oracle.

### tsleep panic path (ranked #8)

An earlier revision of this file recorded #8 as an accepted rp2040 limitation
on the ground that `splnet()` after `splhigh()` opens a real interrupt window
on pic32 and stm32. That holds for pic32, where `splnet()` is
`mips_intr_enable()`. It does not hold for stm32: `arch/stm32/include/intr.h`
defines `splnet()` as `splraise(IPL_NET)`, which writes BASEPRI_MAX, and
BASEPRI_MAX takes effect only when it raises masking, so after
`splraise(IPL_HIGH)` it changes nothing; that header's Cortex-M0 branch
defines `splnet()` as a global disable instead. On rp2040 `splnet()` is
`arm_intr_disable()` because PRIMASK masks all or nothing. The window existed
on one of the three machines.

The resolution is a machine-independent policy rather than a port-local guard:
no interrupt runs between the panic test and the return. Every console putc in
the tree polls its device under `spltty()` -- `uartputc()` in
`arch/pic32/dev/uart.c`, `arch/stm32/dev/uart.c`, and `arch/rp2040/dev/uart.c`,
`usbputc()` and `usbdrain()` in `arch/rp2040/dev/usb.c` -- so the panic message
reaches the user without an interrupt, and `boot()` opens its own window before
`sync()` on all three machines, which is where panic-time device work is
driven. The `splnet()` and `noop()` calls are gone from `kern_synch.c` and the
comment states the mechanism. `noop()` now has no call site; its definitions
remain in the three `machparam.h` headers.

## Resolved on hardware in the integration pass

Every item below was built into one image, flashed, and exercised on the
board; the negative controls ran on the previous kernel from the same tree.

### Signal frame no longer aliases the exception frame (ranked #1)

`sendsig` reserved four words below the sigcontext; the SVC return path
writes the eight-word hardware frame at tf_sp, so the frame covered
sc_onstack, sc_mask, sc_r0 and sc_r1. Negative control on the old kernel,
tests/rp2040/sigtest: after one caught SIGALRM, sigblock(0) returned
0x20000475 (the sigtramp address) and the interrupted sigsuspend returned
with errno 0x20000576. struct sigframe now reserves the eight frame words,
sigreturn reads the context at tf_sp + 32, the frame base is eight-byte
aligned, and the handler enters with XPSR STKALIGN clear while sc_psr keeps
the original bit. On the fixed kernel all nine sigtest checks pass;
kern.systrace=2 shows sc = frame + 0x20 and interrupted sp = frame + 104.

### Smaller C call sites keep SP 8-byte aligned (ranked #2)

cgthumb.c pads the argument block when the pushed word count is odd and
reclaims pad plus arguments after the call; the prologue was already
8-aligned. Oracle: usr.bin/smlrc/tests/t17_align (31 probes) and
spalign.py over the generated corpus: 49 misaligned calls of 242 before,
0 after; differential fuzz 60/60 seeds match the host. Code-size cost
+0.04 to +0.45 percent per program.

### Exception entry stubs are naked functions (ranked #3)

PendSV_Handler, SysTick_Handler and HardFault_Handler carry
`__attribute__((naked))` and an explicit exception return. At -O0 the
previous form emitted `push {r7, lr}; add r7, sp, #0` before the entry
sequence, which moved MSP under the clockframe and faultframe reads; with
the attribute the first eight instructions are identical at -O0, -O and
-O2. On the board: boot, login, sigtest, fptest, streamtest, CoreMark
241.93 (242.30 before, noise), and an unaligned `str` in
tests/rp2040 scratch reports "fault: HardFault, exception 3" with a sane
register bank and exits 139 with the shell alive.

### Bulk IN arms avoid the RP2040-E15 window (ranked #7)

usb.c defers the AVAILABLE write when the microsecond delta since the last
SOF is inside the upstream TinyUSB window (800 to 998 us), enables DEV_SOF
for the configured lifetime, and arms the held word from the SOF handler.
Counters machdep.usb_e15_deferred and machdep.usb_bulkin_arms are sysctls.
On the board a 140 KB console transfer raised the deferral counter from 7
to 11 and `cat /usr/bin/awk | cksum` matched the host checksum
1679832249 52724. The VL805-host corruption case itself is not reproduced
here; the guard is proven active and byte-preserving, not proven necessary
on this host.

### tsleep panic path keeps interrupts masked (ranked #8)

The ineffective splnet() and noop() are gone; every console putc in the
tree polls its device under spltty(), and boot() opens its own splnet()
window before sync(). The earlier note that stm32 lowered to a network IPL
there was wrong: splraise(IPL_NET) after splraise(IPL_HIGH) changes nothing
under BASEPRI_MAX. Not run on hardware: the panic branch itself.

### PendSV is pended with a direct write-one-to-set store (ranked #18)

SVC_Handler stores SCB_ICSR_PENDSVSET instead of a read-modify-write that
carried the write-one-to-clear PENDSVCLR and PENDSTCLR bits back.

### Native ROM float qualified on the board (gate 4 / ranked #25)

The resolver read the byte before every floating table as its length; only
the single table carries one, and on the V3 ROM the byte before the double
table is 0, so every double entry failed and fptest exited 70 silently.
tests/rp2040/romprobe prints the header and tables the resolver walks
(version 3, SF at 0x1cc with length byte 0x20, SD at 0x24c with byte[-2]
0); the resolver now uses the SDK's fixed 0x54 (V1) and 0x80 (V2+) sizes
and fptest prints FPTEST OK for both corpora. fptest shrank from 14092 to
8340 bytes once libgcc soft-float and the --wrap seam left the link.

### Console trace facility (instrumentation the risky items needed)

kern.systrace (1 = syscalls with names, arguments and results; 2 = sendsig
and sigreturn frames) and kern.systracepid, under "options SYSTRACE",
56 text bytes on PICO because syscallnames[] was already linked. This is
the ktrace substitute for a 96 KB window: no trace file, no kdump.

### libc footprint (ranked #21, #22 and the size audit)

doprnt drops the kernel-only conversions, re(1) stops forcing the
printf-float conversion into its link, and the audit in
libc-size-audit.md (rpi notes repository, research/discobsd-rp2040/) records 25166 bytes off the root across 28 shipped
programs with the rejected candidates and their measured reasons.
NSTATIC=8 is gated by tests/rp2040/streamtest, which passes on the board.

## Deferred, with the reason each stays open

- A forced-collision `divrace` oracle with a negative control remains the
  attended hardware gate for the no-divider-owner invariant. A future
  kernel, IRQ, NMI, callout, preemptive switch, or second core divider
  consumer invalidates direct ROM division.
- The panic branch of tsleep (ranked #8) has a source argument, not a
  hardware run.
- The duplicate-BSS-clear (ranked #12), the `resume` exchange loop (#13),
  and the optimization items #14-17, #19, #20, #23, #26-30 remain behind
  exact relinked measurement. tools/analysis carries the audit's analyzers
  for that work.
- ctime's 2036-byte static state plus a tzload alloca of the same size is
  the largest remaining libc cost; shrinking it needs a decision about
  zoneinfo on the board (libc-size-audit.md in the rpi notes repository).
- The curated man-page archive (task) remains a decision about 42728
  bytes of root.

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
