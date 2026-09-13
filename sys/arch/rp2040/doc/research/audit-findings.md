<!--
Verbatim copy of the read-only audit ledger FINDINGS.md (SHA-256
7ae8cf27fe2b7c0a847580e3b84476b865041ae84da6b49dc509dbb6a7d61130 at
capture), with local filesystem paths replaced by descriptions. The
response ledger is audit-response.md; the ranked items it resolves or
defers refer to the section names below.
-->

# DiscoBSD RP2040 read-only audit ledger

Scope: the primary checkout at the clean baseline commit
`ec3b70c351f330560f1b110a1409395ae70d0ba9`, retained linked image
`sys/arch/rp2040/compile/PICO/unix`, Pico SDK 2.3.0, and the local RP2040
documentation corpus.  The linked-image audit began at
`60a59a1105fb63c2c221f7bf51f47f8fb45061e0`; every RP2040 implementation
path under `sys/arch/rp2040` outside `doc/` remained byte-identical between
that commit and the clean baseline.  Concurrent work advanced `main` and
rebuilt artifacts during final verification.  The live-delta section binds
those later observations to explicit source and binary hashes rather than
silently transferring the baseline's machine-code claims.  Repository, build,
service, network, and device writes are outside the audit boundary.  Scratch
reports and analyzers reside under the audit scratch worktree (analyzers now under tools/analysis).

## Retained audit baseline

- Checkout observation: clean `main` at
  `ec3b70c351f330560f1b110a1409395ae70d0ba9`; one registered worktree.
- Binary: 801652 bytes; SHA-256
  `c9feb49b16deb822eaa6c3d8a790a1851993974c1ab7d80278c796dcb369f777`;
  mtime 2026-09-12 10:49:30.253203938 -0700.
- Canonical manifest input `distrib/rp2040/mi.rp2040`: SHA-256
  `65ce53f9b0eab1e421b3b6178da4865f672d4bf3f73e1d2bef93e7b12b1757bb`.
  The input added `/usr/bin/coremark` after the retained audit baseline was
  built.  Baseline recovery sums therefore exclude CoreMark.  A later generated
  root and flash-region chain includes the exact current CoreMark bytes, as the
  live-state observations record separately.
- Source drift after the linked-image baseline consists of Bubble changes,
  CoreMark sources and manifest admission, and RP2040 research documentation.
  The audit binds machine-code claims to the retained image and implementation
  claims to the byte-identical RP2040 sources, rather than claiming that the
  retained image represents the new CoreMark root.
- Primary documentation: RP2040 datasheet dated 20 February 2025, SHA-256
  `be56fbb75ba0ae9e26558a73c93ac3e75c2ad4e6878d3b6703de2a76d886ea8c`;
  Pico SDK package 2.3.0.
- Scoped guidance: the repository contains no `AGENTS.md`; the user-supplied
  global instructions apply.
- Hardware authority: the operator-login, password-free wheel `su`, root-login
  refusal, SwapRAM, editor, shell, and userland behavior in the request are
  user-supplied board observations.  The audit does not relabel those
  observations as independently reproduced host evidence.

## Late live-state observations

- Git reports one registered worktree on `main` at `f24e82ba`.  Commit
  `f24e82ba` changes only CoreMark's Makefile and `core_portme.h`; the committed
  divider sources, CoreMark sources, and manifest match `HEAD`.  Fourteen
  tracked generated PICO/PICO_UART build entries remain modified.  Six
  untracked build outputs remain: CoreMark, Stevie, and four PICO dependency
  files.  The audit preserves every path because concurrent writers own their
  provenance.
- Final ref resolution gives `main`, `pico/main`, and `pico/HEAD` the same
  `f24e82ba` object.  The remote-tracking reflog records external push updates
  for `0a09de3f` at 14:25:42 and `f24e82ba` at 14:34:35.  The audit performs no
  fetch, push, or ref write.
- PICO is 801760 bytes, SHA-256 `e69a97e3...`, with mtime
  2026-09-12 14:19:22.910376763 -0700.  PICO_UART is 735696 bytes, SHA-256
  `3581129c...`, with mtime 2026-09-12 14:03:21.310362692 -0700.  Repeated
  before/after hashes remained identical across the final static replays.
- The first build-process poll found no active compiler or build driver.  A
  separate CoreMark serial reader appeared and exited without any audit action.
  A later poll observed another process run `bmake cleanfs`, `bmake flash`,
  `picotool load`, and board reboot commands.  That process exited at 14:31,
  after creating the 3145728-byte `distrib/rp2040/flash.uf2` with SHA-256
  `f07cd948b3271ea81055856930398858731f784aa73c5f05f491f0c371927519`.
  The DiscoBSD serial symlink then reappeared as `/dev/ttyACM0`.  The audit
  neither launched, controlled, nor attached to either concurrent sequence.
- The generated `distrib/rp2040/_manifest` hashes to
  `5d1c26de717462583b6dd8c8579aae7f364f3dfeaac9ea0bc3e19f219fffc623`, contains
  `/usr/bin/coremark`, and omits `fptest` and `divrace`.  The 1012736-byte
  `sdcard.img` hashes to
  `51f0f7fb95eceaf4d311f8d0ec3e74f0e75559ecb6ed31bc37c9a5ad283e5028`;
  read-only `fsutil --check` reports 95 files, 858 used blocks, and 113 free
  blocks.  A bounded filesystem parser resolves `/usr/bin/coremark` to inode
  42, mode 0100775, size 16732, and SHA-256 `7e8ddfca...`, exactly matching the
  standalone binary.  The superblock records 161 free inodes.
- An in-memory `flashimg` replay of `sdcard.img` hashes to
  `372f475c489e7a61bb5ae44ee87d2caf2649fc389b28ee039904d99b8350f200`,
  exactly matching the stored 1572864-byte `flash.bin`.  Parsing all 6144 UF2
  blocks reconstructs the same bytes from contiguous 256-byte payloads at
  `0x10020000..0x1019ffff`.  This proves the artifact chain from the current
  root image through `flash.uf2`; the separate process observation supplies
  the load-and-reboot provenance, while post-reboot behavior remains
  unobserved by the audit.
- Commit `f24e82ba` changes CoreMark's `HAS_FLOAT` default from one to zero and
  removes `PRINTF_FLOAT=yes` from its Makefile after the binary replay.  The
  committed header hashes to `9a6e3ef6...`; its new comment reports a
  float-formatter stack overflow in a 96 KiB process window.  A concurrent
  rebuild produced a 16732-byte OMAGIC CoreMark image, SHA-256
  `7e8ddfcae326ec39bd2bc37298cd939882368bad661896e5aa6a685f997f81d5`,
  with 16264 text, 436 data, 100 BSS, and zero static floating-format tokens.
  `HAS_FLOAT=0` omits the canonical `CoreMark 1.0` score line, and the unsigned
  `time_in_secs` path truncates fractional seconds before the rate division.
  The missing hashed pre-change image, fault frame, and serial transcript
  prevent an exact recovery, overflow-causality, or board-score claim.  Both
  PICO kernel image hashes remained unchanged.
- Contrary to the earlier handoff, `python3 pico-host/discobsd-web` is running
  from the host notes directory and listens on `127.0.0.1:7681`.  The audit
  reads process and socket metadata only; it neither connects to nor changes
  the web console.
- Active audit scripts, indexes, reports, and the ledger reside under
  the audit scratch worktree (analyzers now under tools/analysis).  Another writer's harness artifacts
  and older staging residue remain under `/tmp`; the audit does not move or
  delete artifacts whose before-state and ownership are outside its authority.
- The user-supplied board observations precede the concurrent final flash.
  Login, SwapRAM, editors, shell behavior, and ROM-divider behavior therefore
  require attended post-flash revalidation before they can be attributed to
  `flash.uf2` SHA-256 `f07cd948...`.
- Two final authority snapshots separated by 15 seconds were byte-identical.
  `HEAD`, `main`, `pico/main`, and `pico/HEAD` remained at `f24e82ba`; one
  worktree, 14 tracked generated modifications, six untracked outputs, all
  eight principal artifact hashes, the empty build-process set, PID 3765030's
  loopback listener, and `/dev/ttyACM0` remained unchanged.  The observation
  measures only those named surfaces.

## ROM-float and divider changes after the retained baseline

Divider verification binds to commit
`0a09de3f0db6d2756db8a15e6111f3c7d4a389e0`.  That commit amends the earlier
`d5e10caada5c07e1be0382281d0c8bb68eda65ac` divider restore.  The committed
source hashes are
`25eed7937ce86bd47da25c95b3e9163f693136c3a96f1fe867f804951924ee4b` for
`rom_float.c`, `aa46da2b69f03a238fc8f5734debe017ee49f352dcfafbd9a54daa9912a0b4a0`
for `sys.mk`, `b58b7ea61c10d845b19b3ffd53d7a5f8e792bf36667fc34394379cd0ff4dd2a7`
for RP2040 `types.h`, and
`2bfb93c0e73d433c40ba5019f41607000d36270a64d764c66e5a0ca994f0f834`
for `locore.S`.  Current `HEAD` `f24e82ba` is a two-file CoreMark-only
descendant, so the working copies of all four divider/float files still match
both the inspected bytes and `HEAD`.

The current 801760-byte PICO artifact hashes to
`e69a97e3cc9b6f75bc9f29c628e84d77b324d340acfde6bef4219167ff20b235`
and encodes the corrected divider-before-register ordering.  The 735696-byte
PICO_UART artifact hashes to
`3581129c49b2bc556c0d6c361cad0561584906eabbbcb51d35ec96de71bbf918`.
PICO_UART retains the superseded defective ordering and therefore does not
represent the committed source.  Earlier concurrent snapshots were the broken
PICO artifact `9201b8fb...` and the corrected intermediate PICO artifact
`31541a4c...`; the current hashes supersede both for live-binary claims.

The committed manifest input `distrib/rp2040/mi.rp2040` retains SHA-256
`65ce53f9b0eab1e421b3b6178da4865f672d4bf3f73e1d2bef93e7b12b1757bb`.
The first late-state poll observed another writer's temporary manifest input at
`75a9ee1116b9b619e1d6e1c195e7d18f1896194916459df478299e0b7e60051b`
with additional `/usr/bin/fptest` and `/usr/bin/divrace` entries.  A later poll
observed the working input restored to the committed hash.  No audited root
image is bound to the temporary input, and the stable PICO bytes span that
transition.  The final generated root contains CoreMark and omits `fptest` and
`divrace`; only the latter two remain outside every recovery denominator.

### PICO_UART retains the superseded saved-r10 pointer defect

- Corrected authority: committed `locore.S:112` keeps the `label_t *` in `r10`.
  Lines 157-170 restore divider values through `env + 40` before line 184
  restores the incoming process's `r10`.  Current PICO executes that order at
  `0x10000ec0..0x10000efa`.  GNU, LLVM, and Capstone agree on the linked path.
- Stale-target defect: PICO_UART restores application `r10` at `0x10000ed2`,
  then copies that value at `0x10000edc`, adds 40, and dereferences it at
  `0x10000ee0`.  An affected `longjmp` or `resume` can load divider operands
  through an unrelated or invalid address.  PICO_UART requires a rebuild from
  `0a09de3f` or explicit retirement before target parity can be claimed.
- Wider ownership gap: `setjmp` snapshots divider values without masking
  interrupts, signal frames contain no divider state, and an IRQ can clobber a
  transaction without scheduling.  The source search found no existing
  divider-using RP2040 ISR, but lexical absence cannot authorize future IRQ,
  generated-helper, indirect, or ROM use.
- Scheduling boundary: `SysTick_Handler` calls `hardclock` and returns;
  `userret` schedules from syscall or fault return.  The claimed ordinary
  timer-preemption interleaving inside a ROM divide has no current control-flow
  witness.  The landed process test does not add one.
- Privilege boundary: `setup_user_mode` writes `CONTROL=3`, including
  `CONTROL[0]=1`.  CMSIS restricts `cpsid i`, `cpsie i`, and PRIMASK writes to
  privileged execution.  A userland wrapper cannot implement interrupt
  exclusion.  Kernel-owned process-switch preservation and cooperative
  interrupt nesting require separate, explicit mechanisms.
- CSR boundary: reading QUOTIENT clears DIRTY, while result writes terminate an
  outstanding calculation and set READY/DIRTY.  The four saved values do not
  preserve exact CSR history.  The restore READY spin adds cost; the SDK restore
  routine overwrites results without that wait.
- Required gate: keep ROM division outside an admitted release until the
  corrected ordering is rebuilt for every retained target, and a forced
  per-process collision oracle, IRQ/signal interleavings, fork/exec/exit
  coverage, and DIRTY/READY transition tests all pass.

### The divider and float harnesses do not prove their advertised claims

- Artifact authority: temporary `divrace.c` hashes to
  `8aceabd4862dd2b319f4847e7b28a030c3aa7e6bab32b17dc1e2550eabd9c72b`;
  its retained ELF hashes to
  `22e3011956cc468515d2576d3cea595040976eedd5c0db5965de5cf22dc7c91b`.
  Temporary `fptest.c` hashes to
  `9e94127c0b3b038fe29957bbe693fc073bf2192bfb484d2a14c7c978f4effabd`;
  its retained ELF hashes to
  `21c005255a26465f94a3b2f733cd9624cac9578d0fd1e6190ae26ef58b88da69`.
  These artifacts remain under another writer's `/tmp/claude-1000` scratch
  tree; the durable audit only reads them.
- `divrace` forks, then each process runs a pure arithmetic loop.  The loop has
  no syscall, yield, PendSV request, interrupt injection, collision counter, or
  interrupted-PC observation.  Current SysTick control flow does not schedule
  there, so the processes can execute sequentially.  More iterations increase
  workload length without proving a switch inside the ROM divider window.
- The retained `divrace.elf` contains one wrapped float divide per iteration;
  the source's second division compiles into multiplication.  Both processes
  expect 1000 from the first division, tolerance ranges accept incorrect finite
  results, and ordered comparisons accept NaN.  Zero or negative iterations
  pass vacuously.  Fork/wait failures, child identity, and `WIFEXITED` are also
  unchecked, so abnormal child termination can be misreported as success.
- `fptest` exercises only eight positive finite operations on 7.5 and 2.5.
  It compares absolute error rather than bits or ULPs, and its error oracle
  calls the wrapped subtraction implementation under test.  A pass supports
  those eight tolerance cases, rather than signed-zero, subnormal, NaN,
  infinity, overflow, underflow, rounding-boundary, ROM-version, or general
  AEABI semantics.
- Both retained harness ELFs report GCC 16.2.0 EABI and contain the GNU-selected
  `__wrap___aeabi_*` symbols.  They prove the host GNU `--wrap` link seam, not
  the native board compiler's OMAGIC a.out linker seam.
- Required replacement: force and count an identified divisor-write-to-result-
  read collision, use distinct bit-pattern oracles, validate child completion,
  and require a deliberately broken preservation variant to fail.  A separate
  raw-bit corpus must classify every intentional ROM-versus-libgcc numerical
  difference and reject single-bit or ULP mutations outside that contract.

### Generic label growth costs 656 linked text bytes

- `label_t` grows from 40 to 56 bytes.  Three instances grow `struct user` from
  972 to 1020 bytes and reduce the fixed 3072-byte u-area's kernel-stack
  allowance from 2100 to 2052 bytes.  Pahole 1.31 reads those exact DWARF
  layouts from the rebuilt kernel.
- The complete recompilation changes the encoded sizes of 103 named functions.
  Their size deltas sum to exactly 656 bytes: `.text` grows from 94401 to 95057
  bytes, while `.data` remains 512 and `.bss` remains 108008.  `setjmp` grows
  24 to 40 bytes and `longjmp` grows 3152 to 3176; widespread larger user-field
  offsets account for the remaining linked growth.
- The initialized flash payload grows from 95169 to 95825 bytes.  The ELF file
  grows only 108 bytes, so ELF file size is not the flash-cost metric.
- A complete design should separate the process-switch divider state from the
  three generic nonlocal-goto labels, preserve existing user-field offsets, and
  measure stack headroom plus compressed-swap effects.  Exact architecture and
  lifecycle proof remains more important than the 656-byte recovery.

### The native compiler cannot select the wrapper symbols

- `share/mk/sys.mk` gives host GNU links eight `--wrap` options.  The board's
  `distrib/rp2040/cc` accepts no `-Wl` option and invokes the native a.out linker
  directly as `ld ... libc.a`.  That linker exposes `-u`, `-e`, `-T`, and the
  traditional strip/relocation switches, but no wrap operation.
- Smaller C emits `__aeabi_fadd`, `__aeabi_fsub`, `__aeabi_fmul`, and
  `__aeabi_fdiv`.  The new board-libc member exports only
  `__wrap___aeabi_*`, so those native references cannot select it.
- The canonical staged board archive remains the 36412-byte pre-change file.
  A separately rebuilt scratch archive measures 37592 bytes and its
  `rom_float.o` member measures 976 file bytes, while the native entry seam
  remains disconnected.  These are future-root measurements, not properties
  of the retained image.
- A native solution needs direct AEABI entry symbols or explicit a.out-linker
  wrap semantics, plus a real source for every V1 double fallback.  The reduced
  board libc does not carry libgcc's `__real___aeabi_d*` implementations.

### Raw ROM arithmetic has a distinct numerical and footprint contract

- The datasheet publishes raw `SF` and `SD` tables with standard ARM EABI
  calling conventions.  Codes, lookup signature, and add/sub/mul/div indices
  0/1/2/3 are correct.  The SDK's warning covers its internal patchable RAM
  tables; it does not invalidate every direct call through the documented raw
  ROM table.
- ROM arithmetic flushes input and output subnormals and maps NaNs to
  infinities.  Global AEABI wrapping therefore changes observable libgcc
  behavior.  V1 double fallback and V2+ ROM execution also differ from one
  another.  Admission needs a declared numerical contract and bit-pattern
  tests for subnormals, signed zero, NaNs, infinities, invalid operations,
  overflow, and halfway rounding.
- The current ELF `rom_float.o` contains one 432-byte text section, 12 BSS
  bytes, and strong relocations to all four double fallbacks.  Selecting any
  wrapper selects the whole section.  The four Cortex-M0+ libgcc double objects
  contain 5300 raw text bytes before their `__clzsi2`, `__aeabi_uidivmod`, and
  jump-table dependencies.  Runtime ROM-version selection cannot remove those
  link-time roots.
- The implementation copies neither ROM table, so the SDK's two 256-byte RAM
  allocations do not apply.  The implementation still consumes two pointers
  and one flag, and exact per-program text, data, BSS, root-block, and cycle
  deltas remain unmeasured.

## Established correctness findings

### Signal frame aliases the Cortex-M exception frame

- Evidence: `sig_machdep.c:34-107` reserves an 88-byte `sigcontext` at PSP.
  PendSV writes its 32-byte hardware-compatible exception frame at that same
  PSP and places the handler stack inside the saved context.
- Consequence: the exception frame overwrites `sc_onstack`, `sc_mask`, `sc_r0`,
  and `sc_r1`; signal return lacks a trustworthy saved context.
- Candidate repair: reserve eight words below the signal context, place the
  context at PSP+32, align alternate-stack top to eight bytes, preserve the
  original xPSR, and control the stacked STKALIGN state.  Evaluate an explicit
  trapframe SP field because PendSV substitutes PSP for stacked r12.
- Falsifier: a byte-range proof showing that every exception-frame write and
  handler stack write lies outside every saved-context byte.
- Future validation: native compiled frame-layout test plus attended signal
  delivery and `sigreturn` test on RP2040 hardware.

### Reset performs 108008 bytes of duplicate BSS clearing

- Evidence: `_sdata=0x20018000`, `_edata=_sbss=0x20018200`,
  `_ebss=0x200327e8`, and `u_end=0x20040000`.  Reset clears 163840 bytes, copies
  512 bytes of `.data`, and clears 108008 bytes of BSS again.
- Consequence: reset issues 271848 clear bytes and overwrites `.data` once
  before restoring it.
- Candidate repair: copy `.data`, then clear `_sbss..u_end` exactly once.
- Falsifier: a reset write-range trace with no duplicate address and final RAM
  bytes equal to the linker contract.
- Future validation: instruction-level emulator trace, then boot timing and
  cold-boot validation on hardware.

### USB Bulk IN lacks the RP2040-E15 mitigation

- Evidence: `usb.h:91` and `usb.c:543-561` expose 64-byte Bulk IN transfers;
  the driver has no SOF handling.  RP2040 B2 silicon is affected.  Pico SDK
  TinyUSB delays Bulk IN availability during the final 200 microseconds of a
  USB frame.
- Consequence: a Bulk IN transfer can exercise the documented E15 corruption
  window.
- Candidate repair: implement the SDK-equivalent frame-window guard in the
  native driver while preserving existing endpoint state transitions.
- Falsifier: source and linked-code evidence of an equivalent time-window guard
  on every affected Bulk IN availability transition.
- Future validation: deterministic controller-state test and high-volume USB
  transfer test on affected B2 hardware.

### SVC priority arithmetic underflows

- Evidence: `intr.h:53,77-79` maps `IPL_SVCALL=IPL_TOP=7` through
  `IPL_HIGH-ipl`.  Unsigned promotion yields priority byte `0x80`.  Linked
  startup code writes PendSV `0xc0`, SVC `0x80`, and SysTick `0x00`, contrary
  to the stated highest-priority SVC contract.
- Consequence: exception preemption ordering differs from the source contract.
- Candidate repair: encode the highest configurable SVC priority directly or
  define a total, range-checked mapping.
- Falsifier: exhaustive mapping of every IPL constant to an in-range priority
  byte with the stated ordering.
- Future validation: native mapping test, disassembly assertion, and exception
  ordering test.

### Zero-length copy boundaries underflow

- Evidence: both `copyin` and `copyout` evaluate `pointer + length - 1` before a
  zero-length fast path.  Semgrep independently matched both expressions.
- Consequence: zero-length operations can reject valid boundary pointers or
  invoke wrapped pointer arithmetic.
- Candidate repair: establish the zero-length contract before end-address
  arithmetic and use overflow-safe range checks for positive lengths.
- Falsifier: exhaustive boundary tests for lengths 0, 1, maximum valid, and
  overflowing ranges.
- Future validation: native compiled tests through the real copy seams.

### Unaligned `bzero(dst, 0)` writes one or more bytes

- Evidence: the alignment prefix loop runs before the zero-length condition.
  Semgrep independently matched the unsafe loop.
- Consequence: a nominal zero-byte operation mutates memory for an unaligned
  destination.
- Candidate repair: return before alignment writes when the length is zero.
- Falsifier: canary bytes around every destination alignment remain identical
  for zero length.
- Future validation: native compiled alignment matrix.

### Exception entry stubs depend on accidental C code generation

- Evidence: exception assembly is embedded in ordinary C functions whose
  correctness requires the compiler to emit no prologue or register traffic
  before the inline assembly.
- Consequence: compiler, flags, instrumentation, or refactoring can corrupt the
  architectural exception frame before the stub takes control.
- Candidate repair: own the entry sequence in a naked function or assembly
  translation unit with an explicit ABI boundary.
- Falsifier: compile-matrix disassembly proves identical first instructions and
  stack behavior under every supported configuration.
- Future validation: linked-image pattern gate and nested-exception test.

### The `tsleep` panic path cannot give interrupts a chance

- Evidence: `kern_synch.c:140-151` calls `splhigh()`, then `splnet()` on the
  panic path.  Every nonzero RP2040 `spl` level maps to `arm_intr_disable()`.
  The linked path at `0x10004ba0..0x10004bc2` executes two global disables,
  one `nop`, and the final PRIMASK restore; no instruction enables interrupts.
- Consequence: the implementation contradicts the panic-path comment and
  cannot service a pending interrupt during the stated opportunity.
- Candidate repair: define the intended panic interrupt policy explicitly.
  If selected interrupts must run, mask forbidden NVIC lines and open a
  bounded globally enabled window; if every interrupt must stay masked, remove
  the ineffective second disable and correct the contract.
- Falsifier: a pending permitted IRQ executes inside the bounded window while
  each forbidden IRQ remains pending until the final restore.
- Future validation: linked-sequence assertion and an attended panic-path IRQ
  test.

### USB startup passes a signed IRQ value to `%u`

- Evidence: `usb.c:65` defines `USBCTRL_IRQ` as the unsuffixed integer constant
  `5`; `usb.c:967-968` passes that `int` through varargs to `%u`.  Cppcheck
  independently reports the signed/unsigned format mismatch.
- Consequence: the call violates the C variadic-format contract even though the
  observed ARM ABI gives `int` and `unsigned int` the same width and the value
  prints as expected.
- Candidate repair: use `%d` for the signed macro or pass an explicitly typed
  unsigned value after choosing one IRQ-number type for the interface.
- Falsifier: the promoted argument type at the call is `unsigned int` and the
  format checker accepts the exact call with warnings treated as errors.
- Future validation: warning-clean target compilation and retained startup
  output.

### Login and host-console documentation describes the retired root login

- Board authority: direct root login is refused; `operator` logs in without a
  password and belongs to wheel; password-free `su` reaches uid 0.
- Source corroboration: `etc/shadow` gives root a nonempty hash and operator an
  empty field; `etc/group:1` lists `root,operator` in wheel; `su.c:104-132`
  makes wheel membership the password-free root gate.  `login.c:238-245`
  refuses root on an insecure terminal.
- Drift: `README.md:90`, `sys/arch/rp2040/doc/USER-ACCESS.md:41`, both
  `discobsd-term` copies at lines 13 and 75, and both `discobsd-web` copies at
  line 13 still advertise blank-password root login.  `USER-ACCESS.md:90-91`
  also says the web console binds every interface by default.
- Code authority: both `discobsd-web` copies default to `127.0.0.1` at line
  279 and reject a non-loopback bind without `--token` at lines 307-311.  The
  loopback status text at line 323 calls the argument `--token FILE`, while the
  parser stores the following argument itself as the shared secret; the help
  wording therefore describes a file interface that the code does not provide.
  The repository and `rpi/pico-host` copies are byte-identical: web SHA-256
  `a61d269a9c99cfdd71508bce6cc72492e4a3c5c5c94ce03b1e003a62ae979da0` and
  terminal SHA-256
  `d4b3e797a601a63bb1e6ba2e983f1a5c67ecdf65d2f277ee1b274f6d852763ef`.
- Consequence: an operator follows a login procedure that the board rejects and
  can misread the web listener's exposure boundary.
- Future repair: update all six copies/surfaces together, change the web
  diagnostic that still calls the endpoint a root console, and describe
  `--token` as a literal secret rather than a filename.  Preserve the loopback
  default and authenticated non-loopback requirement.
- Falsifier: every named user-facing surface states operator login followed by
  wheel `su`, and every bind description matches the argument parser and
  refusal path.

## Established footprint and performance opportunities

### Relocate the USB TX ring into both scratch banks

- Evidence: `usbd.tx_ring` occupies 8192 bytes at `0x20018260`; SRAM4 and SRAM5
  form a contiguous 8192-byte region at `0x20040000..0x20041fff`.
- Effect: recover exactly 8192 bytes of striped main-bank BSS and isolate USB TX
  traffic from the four-way striped main SRAM.
- Constraint: linker placement, DMA reachability, simultaneous core access,
  and the boot stack/scratch ownership contract require proof.
- Future validation: link-map assertion, DMA/CPU address test, USB saturation,
  and bank-contention timing.

### SwapRAM admission reserves worst-case compressed space

- Evidence: `swapram.c:189-225` reserves the worst-case contiguous allocation
  before compression.
- Effect: compressible process images can fall through to flash despite enough
  exact compressed capacity.
- Candidate repair: deterministic counting encode, exact allocation, then real
  encode; assert count/write equivalence.
- Future validation: corpus matrix around pool-fragmentation and exact-capacity
  boundaries, followed by hardware fork/restore stress.

### `resume`/`longjmp` contains a 3072-byte exchange expansion

- Evidence: the post-divider linked symbol `resume`/`longjmp` spans `0xc68`
  bytes, or 3176
  bytes.
- Evidence: `locore.S` expands a 12-byte exchange body 256 times, so the
  3072-byte user-area exchange accounts for 96.7 percent of the symbol.
- Effect: a counter-controlled two-word or three-word exchange loop can replace
  the 3072-byte expansion.  Loop code, alignment, and final layout make 3072
  bytes an expansion size rather than a guaranteed net recovery.
- Constraint: prove the complete saved-register, stack, status, and return-value
  ABI before replacing generated straight-line code.
- Future validation: static instruction-equivalence proof and process-switch
  stress.

### UART transmits one byte per interrupt

- Evidence: `uartstart` dequeues at most one byte before enabling TX IRQ, and
  `uartintr` calls `ttstart` once.  The enabled PL011 FIFO is 32 entries deep.
  RP2040 datasheet sections 4.2.2.4, 4.2.6.3, and UARTIFLS Table 434 define a
  programmable TX interrupt threshold.
- Effect: at 115200 baud the one-byte path can approach 11520 TX interrupts/s.
  Selecting the one-eighth-full trigger and refilling from at most four bytes
  to full admits up to 28 bytes per steady-state interrupt, or approximately
  411 interrupts/s.  The initial fill can hold 32 bytes.
- Constraint: preserve stop/start, flush, wakeup, and latency semantics.
- Future validation: interrupt counts, throughput, latency, and tty control-flow
  tests.

### SVC uses a read-modify-write on a write-one-to-set register

- Evidence: SVC executes `ICSR |= PENDSVSET`.
- Effect: a direct write removes a PPB read and avoids coupling to unrelated
  ICSR status bits.
- Future validation: linked instruction assertion and SVC-to-PendSV test.

### Core-local hardware division requires kernel-owned state preservation

- Evidence: current linked integer-division helpers occupy 304 bytes.  RP2040
  datasheet section 2.3.1.5 and SDK `hardware/divider.h:17-25` define one
  eight-cycle divider for each core.  A new operand write immediately squashes
  the calculation already in progress on that core.
- Privilege constraint: linked `setup_user_mode` writes `CONTROL=3`, selecting
  unprivileged Thread mode and PSP.  CMSIS `cmsis_gcc.h:794-811` and
  `cmsis_clang.h:621-638` state that `cpsie i` and `cpsid i`, which write
  PRIMASK, execute only in privileged modes.  A userland shim therefore cannot
  mask interrupts or write PRIMASK as its ownership mechanism.
- Ownership scope: the divider state is core-local, rather than one peripheral
  shared directly across both cores.  Same-core interrupts and process context
  switches can still overwrite a process's dividend, divisor, quotient, and
  remainder.  Cross-core calls do not collide unless software migrates dirty
  state between cores or introduces another shared mechanism.
- SDK authority: `hardware/divider.h:468-513` snapshots all four values and
  restores them; the header also warns that low-level asynchronous routines do
  not follow the SDK save/restore convention and are generally unsafe in
  interrupt handlers.
- Required prerequisite: kernel-owned divider context preservation must cover
  scheduling and every kernel/interrupt use before a general userland
  substitution.  A cooperative wrapper remains a separate design that needs
  proof for arbitrary preemption, non-LIFO scheduling, signals, and process
  termination before restore.
- Opportunity: an admitted wrapper or kernel path can compare the eight-cycle
  native operation against the 304-byte software-helper surface.  Neither the
  wrapper's net linked bytes nor workload cycle savings are measured.
- Falsifier: an exception/context trace preserves all four values across every
  same-core preemption and termination path, or proves that no admitted
  interleaving can touch divider state.
- Future validation: call-site inventory, process-context ABI design, nested
  interrupt and adversarial scheduling tests, relinked size comparison, and
  latency benchmark.

### Raw swap partial writes amplify flash traffic

- Evidence: a partial write performs a 4096-byte read-modify-erase-program
  cycle.
- Effect: small writes incur latency and flash wear disproportionate to payload.
- Candidate direction: coalesce or align writes only after the swap consistency
  contract and power-loss behavior are specified.
- Future validation: traced erase/program counts and fault-injection tests.

### XIP counters can select SRAM relocation candidates

- Evidence: RP2040 datasheet section 2.6.3.6, Tables 158 and 159, defines exact
  32-bit saturating `CTR_HIT` and `CTR_ACC` counters; any write clears each
  counter.
- Effect: measurement can rank hot flash-resident paths before scarce SRAM is
  assigned.
- Future validation: representative workloads with counter deltas and a linked
  placement assertion.

### ROM word-copy and word-set routines are available

- Evidence: RP2040 datasheet section 2.8.3.1.2, Table 165, exports `_memset4`
  and `_memcpy44`.  `_memset4` requires a word-aligned destination;
  `_memcpy44` requires word-aligned source and destination and has undefined
  behavior for overlap.
- Effect: selected aligned bulk paths may reduce flash code and cycles.
- Constraint: ROM revision/API lookup, alignment, overlap, and interrupt
  behavior must match each caller.
- Future validation: ABI lookup test, edge-size matrix, linked size comparison,
  and cycle benchmark.

### Boot ROM floating point is valuable but not a zero-byte tail call

- Primary authority: RP2040 datasheet section 2.8.3.2 supplies
  single-precision routines on every Boot ROM revision and double-precision
  routines only on V2 and later.  The ROM flushes denormals, maps NaNs to
  infinities, supports round-to-nearest/even only, omits traps, limits direct
  trigonometric input ranges, and documents SDK range-reduction wrappers.
- SDK table cost: `SF_TABLE_V2_SIZE` is `0x80`, or 128 bytes.  SDK
  `float_init_rom_rp2040.c` and `double_init_rom_rp2040.c` each declare
  `uint32_t [SF_TABLE_V2_SIZE / 2]`, reserving 256 SRAM bytes per table, while
  each V2 copy transfers 128 ROM-table bytes.  A DiscoBSD design need not copy
  the SDK layout, but a claim of zero RAM overhead fails against the reference
  implementation.
- API constraint: both SDK initializers state that their internal table
  pointers are incomplete and unsafe for arbitrary calls.  SDK AEABI wrappers
  add Boot ROM revision compatibility, NaN/range behavior, conversion
  semantics, and divider protection.  Direct `float2int` floors negative
  fractions while `__aeabi_f2iz` truncates toward zero.
- Divider constraint: ROM division and transcendental paths use the core-local
  divider.  The unprivileged userland constraint and kernel-owned preservation
  prerequisite from the preceding finding apply.  Divider-free add, subtract,
  and multiply remain the smallest candidates; double operations still need a
  V2 check and V1 fallback policy.
- Opportunity: ROM code can remove substantial software arithmetic and improve
  cycles, but table/shim/fallback/support costs and current caller composition
  leave net bytes unmeasured.  Preserve the opportunity without treating ROM
  residence as a free drop-in replacement.
- Research-document correction: `float-libs.md:501-507` calls the SIO divider a
  single peripheral.  The datasheet and SDK define one divider per core; the
  actual hazard is dirty core-local state across same-core interrupts and
  context switches.
- Falsifier: an ABI-complete shim matches the declared IEEE/AEABI behavior over
  edge cases, handles Boot ROM versions, survives adversarial preemption, and
  produces a measured linked reduction and cycle improvement.
- Future validation: per-operation wrapper census, V1/V2 behavior matrix,
  floating edge-case differential tests, context-switch stress, and exact
  linked-size/cycle measurement.

### PRIMASK helpers carry 211 redundant instruction barriers

- Evidence: the linked image contains 217 `ISB` instructions.  DWARF provenance
  assigns 89 to `arm_intr_disable`, 11 to `arm_intr_enable`, and 111 to
  `arm_intr_restore`.  Pico SDK 2.3.0 and CMSIS implement the matching
  `CPSID`, `CPSIE`, and `MSR PRIMASK` operations without `ISB`.
- Effect: matching the native SDK/CMSIS sequences removes exactly 844
  instruction bytes and 211 pipeline flushes.
- Constraint: retain four explicit SCB synchronization barriers and two
  user-mode transition barriers unless separate architectural proof discharges
  them.
- Falsifier: an ARMv6-M architectural rule or instruction-level test shows a
  required post-PRIMASK synchronization effect that the SDK/CMSIS sequence
  lacks.
- Future validation: compile-time sequence assertions plus interrupt entry and
  nested-restore stress.

### Thirteen ignored interrupt-mask saves are machine-dead

- Evidence: whole-function CFG liveness classifies all 102 linked PRIMASK
  reads.  Thirteen ignored `arm_intr_disable` or `arm_intr_enable` return
  values are overwritten on every path before any read.  GNU and LLVM decode
  the same four-byte `MRS` at all thirteen addresses, and DWARF ties the sites
  to `main`, `postsig`, `setsigvec`, `sigprocmask`, `sched`, `arm_fault`,
  `boot`, and `syscall`.
- Effect: void set/clear helpers for callers that discard the old mask have a
  52-byte linked instruction ceiling, independent of the ISB opportunity.
- Constraint: four call/return-boundary liveness cases remain outside the
  count; every caller that later restores PRIMASK must retain its save.
- Falsifier: machine-level dataflow from any admitted `MRS` destination reaches
  a read before overwrite on a feasible path.
- Future validation: native helper variants, linked-address assertions, and
  nested interrupt-state tests.

### RP2040 atomic aliases replace twelve software MMIO RMW sites

- Evidence: `uart_enable`, `uart_disable`, `uart_irq_enable`, and
  `uart_irq_disable` expand at seven linked call sites into a register read,
  bit operation, and write.  Startup contains five more software RMW sequences
  for two system-PLL power clears, two USB-PLL power clears, and the LED pad
  output-disable clear.  RP2040 datasheet section 2.1.2 and SDK
  `hardware/address_mapped.h` define `+0x2000` set and `+0x3000` clear aliases;
  the UART bus interposer implements the same atomic contract.
- Effect: every alias update removes one peripheral read and prevents a lost
  concurrent bit update.  The five fixed-address startup sequences each shrink
  locally from 10 instruction bytes to 6, for a 20-byte pre-layout ceiling.
- Constraint: dynamic UART alias-address formation can consume instruction
  savings, so the seven UART sites carry a bus-transaction claim rather than a
  byte claim.  `uart_pin_setup` mixes a clear and a set and would require two
  alias writes, so it remains outside the twelve-site set.
- Falsifier: a register lacks alias support, an alias changes reserved-bit
  behavior, or a relinked site retains the original MMIO read.
- Future validation: linked address assertions, reserved-bit checks, and
  UART interrupt/foreground concurrency stress.

### Smaller C violates AAPCS call-site stack alignment

- Evidence: `cgthumb.c:1411-1450` pushes every argument as one word, loads the
  first four words into `r0` through `r3`, and leaves later words at `sp`.
  Calls with an odd number of stack-passed words therefore enter the callee
  with `SP mod 8 == 4`.  The checked-in generated `thumb-native.s` exhibits
  the case at its five-argument `printf` call, and
  `README.rp2040.md:74-81` documents the four-byte-only limitation.
- Consequence: the native compiler breaks the AAPCS public-interface contract;
  a GCC-compiled callee may rely on eight-byte alignment independently of the
  RP2040 instruction subset.
- Candidate repair: reserve and reclaim one padding word around calls whose
  stack-passed argument area is four modulo eight while keeping argument five
  at call-time `sp`.
- Falsifier: generated control-flow and stack accounting prove `SP mod 8 == 0`
  at every direct, indirect, helper, and variadic call boundary.
- Future validation: assembly oracle for argument counts zero through ten,
  structure arguments, nested calls, and differential execution.

### Link-time Thumb-1 size candidates

- Evidence: the final mapping-aware PICO image contains 341 inverted-condition
  plus unconditional-branch pairs.  Forty-six fit the inverse short-branch
  range without layout changes.  The fixed-point model admits 47 in its first
  iteration, including `swapout` at original displacement 256 after its own
  second instruction disappears, then admits `mountfs` at original
  displacement 258 after the prior removals.  The final set is 48 pairs and the
  instruction-byte ceiling is exactly 96 bytes.
- Rejected estimates: all 1304 PC-relative literal addresses fail the strict
  same-section `ADR` replacement test.  The only load/add/pop triple is
  `ttread` reloading its return value.  The earlier 56-, 220-, and 572-byte
  estimates do not survive mapping-aware analysis.
- Constraint: literal-pool and section alignment can absorb linked-image
  savings; 525 short-range `BL` sites establish range only and do not prove
  legal sibling calls.
- Falsifier: final-layout branch displacement leaves the Thumb conditional
  range or any transformed pair changes its original two-way CFG.
- Future validation: compiler or linker relaxation, exact relinked section
  delta, and native behavior tests.

## Constrained-C and userland footprint results

### `NSTATIC` 20 to 8 removes 252 source-object bytes per proven payer

- Source arithmetic: `FILE` is 20 bytes on the observed ARM ABI.
  `findiop.c` changes `_iob` from 400 to 160 initialized bytes and `sbuf` from
  20 to 8 BSS bytes, yielding 240 data plus 12 BSS bytes per positive image.
- Manifest denominator: 21 distinct audited manifest images contain positive
  standard-findiop witnesses.  Their source-object sum is 5040 data plus 252
  BSS bytes, or exactly 5292 bytes.  CoreMark contributes the twenty-first
  252-byte payer: its linked `_iob` spans 400 initialized bytes and `sbuf`
  spans 20 BSS bytes.  Five inspected images lack a positive witness, while
  ten remaining manifest paths lack either a positive or negative linked
  witness.
- Claim boundary: 5292 bytes sums distinct executable definitions.  OMAGIC BSS
  consumes process residency but zero root-file payload.  Filesystem block
  packing, relinked segment deltas, simultaneous process count, earlier dynamic
  FILE allocation, and allocation metadata remain unmeasured.
- Rejected claims: the current evidence rejects 15 KiB as a measured current
  root saving and rejects approximately 6.5 KiB as an admitted total.  The
  earlier 25-image source-object ceiling was 6300 bytes.  The current 26-image
  inspected ceiling is 6552 bytes, while the positive set is 5292 bytes.
- Safety gate: reducing static slots from 20 to 8 leaves five slots beyond
  stdin, stdout, and stderr.  A ninth active stream enters `_f_morefiles`, whose
  allocation and caller failure paths require targeted tests.
- Falsifier for the 252-byte arithmetic: the target `FILE` layout or either
  affected array extent differs from the measured 20-byte, 20-to-8 model.
  Relinked segment deltas and additional manifest payers update separate
  measurements without invalidating the 21-image source-object sum.
- Evidence owner: `step5-nstatic-handback.md`.

### CoreMark integer reporting is compact but truncates the reported rate

- Current artifact: the 16732-byte OMAGIC `benchmarks/coremark/coremark` hashes
  to `7e8ddfcae326ec39bd2bc37298cd939882368bad661896e5aa6a685f997f81d5`.
  Its header records 16264 text, 436 data, 100 BSS, zero symbols, and entry
  `0x20000545`.  The final root contains the exact bytes at
  `/usr/bin/coremark`, and the verified Dhara/UF2 chain carries that root.
- Static format closure: commit `f24e82ba` removes the local
  `PRINTF_FLOAT=yes` root and defaults `HAS_FLOAT` to zero.  Source and
  one-byte-minimum binary-string searches find zero floating conversions and
  retain integer `%d` timing/rate formats.  A compiler override can change
  `HAS_FLOAT`, and the stripped OMAGIC image prevents a symbol-level proof that
  every float-converter machine byte is absent.
- Reporting defect: upstream `core_main.c` guards the canonical
  `CoreMark 1.0 : ...` score line with `HAS_FLOAT`, so the current configuration
  omits that line rather than printing an integer form.  `secs_ret` becomes
  `ee_u32`; `time_in_secs` divides microseconds by 1000000 before the subsequent
  iteration-rate division.  Fractional seconds and rate therefore truncate.
  The port comment's claim that both values remain exact is false.
- Provenance boundary: the commit message reports a 24452-to-16732 shrink, a
  float-formatter stack overflow, successful CRC validation, and board rates.
  The audit has no hashed 24452-byte image, retained fault frame, or serial
  transcript.  Those observations remain provenance-limited rather than
  current reproducible evidence.
- Completion gate: report elapsed microseconds and a scaled or rational rate
  without float-stack use, state whether the canonical score-line contract is
  intentionally waived, retain before/after binaries and root-block counts,
  and capture the failing and corrected board transcripts with a stack
  high-water measurement.

### Twenty printf-float roots are source-removable candidates

- Finite result: 30 build rows force `__doprnt_cvt`; source closure classifies
  8 as required, 20 as removable candidates, and 2 as unknown.  `picoc` retains
  a dynamic-format grammar gap and `tclsh` lacks the admitted libtcl closure.
- Linked denominator: 12 removable rows with retained ELFs each contain a
  1328-byte `__doprnt_cvt` symbol, totaling 15936 raw converter-symbol bytes.
  Eight removable rows have unmeasured symbol extents.  Support code, constants,
  alignment, archive co-retention, and final filesystem blocks remain outside
  the 15936-byte count.
- Notable cases: `re` retains the converter while defining zero printf-family
  symbols or direct calls.  UUC requires floating output in `uucico` and `uuq`,
  while eleven other UUC executables inherit the shared flag without a proved
  floating format.
- Claim boundary: source removability does not prove linked recovery.  The next
  pass must remove each explicit root, resolve archive membership, relink, and
  compare exact text/data/file blocks without adding unrelated candidate sums.
- Falsifier: a reachable floating or unbounded format reaches a row classified
  removable, or a relink retains the converter through another root.
- Evidence owner: `step5-printf-float-handback.md`.

### Section-GC analysis exposes 2218 raw bytes but proves no recovery

- Replay: `section_gc_reachability.py` starts at `Reset_Handler`, adds allocated
  data-pointer roots, decodes mapping-aware Thumb control flow, and classifies
  the retained kernel.  The pass finds 535 function groups, 511 reachable
  groups, and 24 candidate groups totaling 2218 symbol bytes.
- Blocking unknown: 85 indirect-control sites prevent a whole-program
  reachability proof.  Candidate groups also contain incoming edges inside the
  unreachable subgraph and functions whose build/configuration ownership needs
  source review.
- Claim boundary: 2218 bytes is a raw symbol denominator, rather than a
  discardable or relinked amount.  The retained build uses `-O` and lacks the
  authorized comparison relink needed to measure tree-wide function sections
  and `--gc-sections`.
- Falsifier: relocation, stored-pointer, indirect-target, linker-script, or
  entry-path evidence reaches a candidate; a relink can also absorb symbol bytes
  in padding or retain their sections.
- Future validation: resolve all 85 indirect sites, compile every translation
  unit with function/data sections, prove selected input-section, alias, and
  relocation ownership, split explicit assembly `.text` where independently
  collectible functions require it, retain startup/linker roots explicitly,
  relink with garbage collection, and run boot plus subsystem regressions.

### The five largest OMAGIC candidates contain zero proven removable bytes

- Bounded denominator: the five largest selected a.out candidate sets are awk,
  fsck, find, getty, and re, totaling 6624 bytes.
- Closed partition: the original filter classifies 1408 bytes as
  archive-associated and co-retained, pending selected-member provenance; 120
  bytes form a reachable `__udivsi3` false positive; 564 bytes are literal/data
  spans; and 4532 bytes remain unknown instruction or padding spans.  The
  independently proven discardable count is zero.
- Decoder correction: normalized symbol-name matching missed a branch into an
  interior divider label.  Numeric containment restores the reachable edge.
  Function boundaries without complete register-target and input-section
  provenance cannot support removal.
- Libgcc correction: withdraw the prior 198-byte signed-division recovery
  claim.  The five occurrences contain 190 bytes in a retained libc section and
  8 bytes of layout padding.  Withdraw recovery interpretations of 8248 and
  12962 bytes.  Preserve 21210 bytes only as the raw OMAGIC symbol-candidate
  denominator across the 25-program set; that total includes unmapped spans and
  the misattributed libc helpers.
- Falsifier: selected-member provenance plus complete direct, indirect,
  relocation, and stored-pointer accounting proves a candidate belongs to an
  independently discardable input section.
- Evidence owners: `step5-omagic-crosscheck-handback.md` and
  `step5-libgcc-idiv-correction.md`.

## Deferred feature audits

### Seven compressed manual pages require 42728 archive bytes

- Evidence: the linked-code Unicorn harness calls the exact heatshrink encoder
  and decoder in `sys/arch/rp2040/compile/PICO/unix` with the production 9/8
  window/lookahead settings.  Every page round-trips byte-for-byte:

  | Page | Raw bytes | Compressed bytes |
  | --- | ---: | ---: |
  | sh | 23603 | 13059 |
  | ed | 20774 | 11598 |
  | awk | 6488 | 3920 |
  | sed | 7130 | 3929 |
  | grep | 5219 | 2976 |
  | find | 6460 | 3818 |
  | cc | 5423 | 3172 |
  | Total | 75097 | 42472 |

- Consequence: the proposed 10240-byte ceiling fails by 32232 bytes, and the
  `sh` page alone exceeds the ceiling by 2819 bytes.  Compressing one combined
  stream produces 42241 bytes, saving only 231 bytes while losing independent
  random access.
- Integrity requirement: bit flips and truncation commonly return decoder
  success with altered bytes or altered length.  Each independent stream needs
  an exact raw-length check and CRC-32/ISO-HDLC; decoder status alone is not an
  integrity oracle.
- Footprint: the linked decoder contributes exactly 566 text bytes, and its
  production state object occupies 590 BSS bytes.  Kernel linkage exposes no
  stable user ABI.  The absent MPU permits physical access but does not make a
  hard-coded kernel address or shared mutable decoder state safe.  A userland
  applet must link its own decoder unless the kernel gains an explicit syscall.
- Existing-program gap: `usr.bin/man/man` is a 12488-byte a.out with 11660
  text, 796 data, and 212 BSS bytes.  Its minimum initial process allocation is
  14716 bytes after the 2048-byte base stack and before argv/environment.  Its
  `cat()` treats both `open()` failure and descriptor zero incorrectly through
  `if (!(fd = open(...)))`, and the paging path gives filenames to `more`
  rather than accepting a decoded stream.
- Documentation gap: the selected `cc.0` describes historical PCC rather than
  the board's constrained shell driver.  Shipping that page unchanged would
  create a false on-device interface contract.
- Bounded design: add `man` to `utilbox`, hard-link `/usr/bin/man` to it, and
  store one `/usr/share/man.db` file.  Existing `pipe`, `dup2`, and `more` stdin
  paths support streaming without a temporary file.  Avoiding new `man/cat1`
  directories preserves directory records: `/usr/bin` has 292 free record
  bytes and `/usr/share` has 988.
- Archive bound: a 32-byte header plus seven 32-byte index entries adds 256
  bytes, so the independent-stream archive is 42728 bytes.  The current image
  has 175 free blocks and 164 free inodes; the archive consumes 42 data blocks,
  one indirect block, and one inode, leaving 132 blocks and 163 inodes before
  applet growth.  Only 280 archive bytes remain before another data block.
- Archive contract: encode magic, version, header and entry sizes, entry count,
  codec, 9/8 parameters, flags, total length, index CRC, and reserved fields.
  Each entry needs a bounded name and section, payload offset, compressed and
  raw lengths, and raw CRC.  The reader must reject arithmetic overflow,
  out-of-file ranges, overlapping streams, duplicate names, wrong codec
  parameters, short or long output, and CRC mismatch.  The generator must
  retain deterministic source hashes.
- Falsifier: a production-codec archive containing all seven exact source pages
  occupies at most 10240 bytes, or an independently accessible alternative
  meets that bound with equivalent integrity and paging behavior.
- Future validation: generate deterministically, mutation-test every header,
  index, range, length, overlap, and CRC failure mode, measure final applet and
  filesystem deltas, and run attended pager tests on the board.

### The integer MLP proposal is a design sketch, not a measured target

- Corrected weight arithmetic for `V=96`, `E=8`, and `k=3`: `H=64` occupies
  8768 bytes with int16 biases or 9088 with int32 biases; `H=128` occupies
  16576 or 17024 bytes.  The documented `H=64` output layer is 6336 bytes, not
  6432.  Biases normally live at accumulator scale and require int32 unless the
  exporter proves safe int16 bounds.
- Vocabulary contract: printable ASCII contains 95 symbols.  A 96-way model
  can encode newline plus printable ASCII, but direct ASCII-byte indexing into
  `[96]` is out of bounds.  Training and inference need one identical
  normalization map plus an explicit policy for tab, carriage return, control
  bytes, and bytes above 0x7e.
- Quantization contract: a 512-entry int16 `TANH_LUT` cannot be assigned to an
  int8 hidden state without an explicit requantization and saturation rule.
  Shifting raw logits to nonnegative values does not preserve the trained
  distribution.  Begin with argmax for the smallest deterministic baseline or
  use max-subtracted integer exponential weights with overflow and zero-sum
  handling.
- Native layout: store `W1[H][k*E]` and `W2[V][H]`.  The proposed `W1[k*E][H]`
  and `W2[H][V]` make each inner dot-product loop stride through memory.
- Compute bound: `H=64` performs 7680 MACs per character and `H=128` performs
  15360.  RP2040 supplies single-cycle `MULS`, while Cortex-M0+ supplies
  neither FPU nor DSP/SIMD dot-product instructions.  Multiplication alone
  therefore bounds execution below by 61.44 and 122.88 microseconds at
  125 MHz; loads, accumulation, activation, output selection, and call overhead
  make actual latency higher and presently unmeasured.
- Residency: OMAGIC loads code and embedded weights into striped SRAM, so
  inference avoids XIP traffic but charges the complete code, tables, model,
  data, BSS, stack, argv, and environment footprint against the 96 KiB process
  window.
- Provenance gap: the tree contains BSD, MIT, public-domain, GPL, and
  Caldera-licensed material.  Training on an unconstrained tree walk is not
  "BSD-licensed by construction."  Define a per-file corpus allowlist, record
  license provenance, and hash the normalized corpus.
- Evidence gap: the repository contains no trainer, exporter, model artifact,
  inference executable, held-out result, quantized-parity result, saturation
  result, or RP2040 latency measurement.  The 12-22 KB and quality claims are
  forecasts rather than observed outcomes.
- Falsifier: a deterministic build produces a warning-clean a.out within the
  declared resident and file-size budgets, and native-versus-host quantized
  parity, held-out bits per character, top-k accuracy, saturation counts, and
  measured board latency meet predeclared thresholds.
- Future validation: use file-separated train/validation splits, deterministic
  seeds, corpus and model hashes, int32 accumulator-bound proofs, byte-domain
  normalization tests, quantized-parity tests, saturation tests, and timed
  inference.  Compare the MLP against a fixed-budget n-gram baseline before
  claiming that learned dense inference earns its extra cycles and code.

## Tool corroboration and limitations

The audit applied the local tool catalog as the local
registry.  The inspected 666-line registry snapshot hashes to
`219fe684a0426908fca46c3e4445b4d0344b849fba3a6fbbd2ca19f1d470b811`.
The registry's `/usr/bin/rg` is currently shadowed on `PATH` by the
Codex-bundled ripgrep; durable command records use `/usr/bin/rg`.  The audit
selected tools by a named evidence question and parked tools whose operation
would cross the read-only boundary or duplicate a stronger format authority.

<!-- markdownlint-disable MD013 -->

| Registry surface | Programs used | Question answered and limit |
| --- | --- | --- |
| Source identity and lexical search | Git 2.55, `/usr/bin/rg` 15.2, ctags/readtags, Global/gtags 6.6.15, cscope 15.9, cflow 1.8 | Bound files, definitions, references, and lexical call sketches.  Tags, Global, cscope, and cflow do not prove preprocessing, runtime reachability, or section ownership. |
| Target compiler and static analysis | ARM GCC 16.2.0 diagnostics, cppcheck 2.21.1, clang-tidy/Clang 22.1.8, Semgrep 1.164.0, sparse 0.6.5-rc1, smatch 0.6.4 | Cross-check eight critical translation units with available target flags.  Legacy callbacks and host/target models limit warning closure. |
| ELF and symbol authority | GNU ARM binutils 2.47, LLVM 22 objdump/readelf, pyelftools 0.33, LIEF package 0.17.3 whose metadata reports 0.17.0 | Establish sections, symbols, mapping markers, bytes, relocations, and ABI metadata.  Raw OMAGIC a.out requires its native eight-word header parser. |
| Independent Thumb decoding | cstool/package 5.0.9; Python Capstone package 5.0.9 whose module reports 5.0.7; radare2 6.2.0; Rizin 0.8.2; LLVM MC | Cross-check boundaries and encodings.  radare2 and Rizin mis-handle selected Cortex-M special-register semantics or sizes, so Capstone plus GNU/LLVM carry semantic authority for those instructions. |
| Bounded emulation and decompilation | Unicorn; one Ghidra 12.1.2 headless import | Unicorn executed exact linked heatshrink routines without target I/O.  Ghidra produced zero useful OMAGIC candidate functions and wrote cache files outside scratch, so the audit stopped further Ghidra use. |
| Root and flash known-format replay | Repository `fsutil` and `flashimg`; bounded BSDFS and UF2 parsers | Check the current root read-only, resolve CoreMark's exact inode bytes, replay Dhara into a pipe, and reconstruct every UF2 payload.  Artifact equality proves the file chain, while neither the parsers nor the observed external loader proves post-reboot behavior. |
| Footprint corroboration | Bloaty 1.1 and Pahole 1.31 (installed but absent from the registry), custom ELF/OMAGIC analyzers | Bloaty independently attributes `longjmp` 3.08 KiB, `usbd` 8.18 KiB, `sr_pool` 64 KiB, and `bufdata` 10 KiB.  Pahole reads the 40-to-56-byte `label_t` and 972-to-1020-byte `struct user` DWARF layouts.  Symbol and type attribution corroborate scale and do not prove removal. |
| Evidence and primary documents | jq 1.8.2, SHA-256, Poppler `pdftotext` | Reconcile finite JSON sets, bind bytes, and inspect the dated RP2040 PDF.  Extracted line numbers supplement printed sections and tables; the PDF hash remains the document identity. |

| Registry surface parked | Programs | Concrete reason |
| --- | --- | --- |
| Structural match and rewrite | ast-grep, tree-sitter, weggli, comby, Coccinelle | Semgrep plus compiler/decoder evidence already closes the named syntax patterns; rewrite machinery adds no stronger read-only proof. |
| Broad metrics/security scans | cloc, scc, lizard, rats | Line counts and generic warnings cannot decide linked bytes, MMIO semantics, privilege, or reachability. |
| Whole-program semantic capture | CodeQL, Infer, scan-build | Target-faithful capture requires rebuilding or wrapping the legacy build, which crosses the current no-build boundary; cheaper analyzers already expose the admitted source defects. |
| Code-property graph | Joern | A scratch database would be boundary-compliant, but legacy K&R C, target headers, and inline assembly require parser/model adaptation.  Existing source, CFG, relocation, and decoder evidence leaves no named residual that a generic CPG uniquely closes. |
| Dynamic tracing and profiling | ARM GDB, GDB, LLDB, rr, strace, ltrace, perf, bpftrace/BCC, trace-cmd, LTTng, SystemTap, DynamoRIO, Valgrind, uftrace, sysprof, Perfetto | Host execution cannot model RP2040 MMIO or exception entry, and target execution is outside scope.  These tools become appropriate for an authorized emulator or hardware-validation pass with explicit stop conditions. |
| Sanitizers and fuzzers | ASan/compiler-rt, AFL++, afl-utils, Honggfuzz, Radamsa, unifuzzer | Every route requires instrumented rebuilding, execution, or testcase mutation.  Future native boundary harnesses should use them after a stable oracle exists. |
| Hardware/debug/serial | OpenOCD, probe-rs, pyOCD, picotool, minicom, picocom, tio, pyserial, usbtop, lsusb/usbhid-dump | The audit launches none of these tools.  A concurrent serial reader is separate, unadmitted activity.  These tools belong to the attended validation lane after offline gates close. |
| Firmware carving and flash | binwalk, unblob, firmwalker, firmware-mod-kit, foremost, bulk_extractor, Sleuth Kit, flashrom/flashprog, UEFI/BIOS tools | ELF, OMAGIC, the root manifest, and filesystem-native parsers provide stronger known-format authority.  Flash tools also add needless device risk. |
| Extra RE and symbolic execution | angr, miasm, KLEE, Qiling, RetDec, Binary Ninja, IDA, JEB, Cutter, r2ghidra/rz-ghidra | Source, symbols, mapping markers, and four independent Thumb decoders leave no named residual that these heavier routes uniquely answer. |
| Unrelated registry domains | GPU/OpenGL, LAN discovery, Windows/PE, host-integrity tools | Those domains do not intersect the RP2040 linked-image, source, SDK, or documentation questions. |

<!-- markdownlint-enable MD013 -->

- Package provenance exposes three launch/version traps.  Smatch package 1.75
  reports executable version 0.6.4.  Python LIEF package 0.17.3 reports
  distribution/module version 0.17.0.  The user-local Joern symlink is
  unowned, while its target belongs to `joern-git` 4.0.614 under `/opt/joern`.
- Installed binwalk is version 3.1.0, so version-2 extraction assumptions do
  not apply.  Known ELF, OMAGIC, and filesystem formats make carving weaker
  than the selected native parsers.  Perfetto is installed, while
  `trace_processor` and `trace_processor_shell` are absent from `PATH`.
- Cppcheck 2.21.1 found the SVC priority conversion and signed USB IRQ format
  mismatch (`%u` at `usb.c:967`).
- Cppcheck's SwapRAM negative-index diagnostic is a model artifact because the
  preceding `panic` path does not return.
- Clang-tidy 22 reported expected MMIO fixed-address and obsolete BSD API
  diagnostics; excluding those explicit model mismatches left zero findings in
  the critical files.
- Semgrep 1.164 independently found both copy-boundary underflows and the unsafe
  `bzero` alignment loop.  Its inline-assembly parsing is partial.
- GCC syntax-only checks with `-Wall -Wextra -Werror` expose legacy unused
  callback parameters.  Adding only `-Wno-unused-parameter` makes the eight
  critical translation units pass; that exception remains technical debt, not
  a warning-clean result.
- Smatch processed all eight critical translation units; every invocation
  exited zero and emitted zero diagnostics.  Sparse reaches the real include
  graph but stops at unprototyped device callbacks and emits host/target
  pointer-width noise.  Neither silent smatch output nor noisy sparse output
  discharges the machine-level findings.
- The final-image mapping-aware histogram is bound to PICO SHA-256
  `e69a97e3...` at 36877 instructions and 77990 instruction bytes: 34759
  16-bit instructions and 2118 32-bit instructions.  The count covers 77554
  executable bytes in `.text`, 232 in RAM-resident `.data`, and 204 in
  `.boot2`.  Capstone consumes every byte in all 393 Thumb ranges.  GNU and
  LLVM produce the identical 36877-address set.  LIEF and pyelftools agree on
  all 877 executable-section mapping tuples and all executable section
  contents.
- The earlier 586-candidate discrepancy came from mnemonic/alias and data
  classification rather than different code bytes.  GNU selective `-d` also
  suppresses the `c0 46` alignment instruction at `0x10000f02` because its
  two-byte `$t` range lies inside the odd-addressed `icode` object.  Bounded
  GNU `-D`, LLVM, Capstone, cstool, and LLVM MC all decode it as `mov r8, r8`
  (`nop`).
- The linked `.boot2` carries one `$d` marker because the build embeds padded
  bytes.  Source labels, literal targets, and bounded disassembly establish
  Thumb code at `0x10000000..0x100000cb`; `0x100000cc..0x100000ff` contains
  literals, padding, and checksum.  GNU and LLVM each decode 95 boot-stage
  instructions from the explicit Thumb span.
- GNU objcopy 2.47 zeroed section contents in a disposable binary-to-ELF wrapper
  when `--rename-section` assigned code flags; LLVM objcopy preserved the exact
  240 input bytes.  The firmware build does not use that wrapper operation, so
  the observation constrains audit-tool selection rather than firmware truth.
- The retained-baseline named-path cross-check covers 15 functions, 17
  mapping-aware code segments,
  and 3129 instructions across reset, context restore, copy primitives, signal
  delivery, exception entry, UART, USB, and SwapRAM.  Capstone, cstool, and
  radare2 agree on every boundary and byte sequence.  Rizin agrees on offsets
  and bytes but reports `size=1` for eight four-byte Cortex-M MRS/MSR encodings;
  radare2 keeps the four-byte sizes but labels the same special-register class
  `invalid`.  Cstool decodes all seven unique byte sequences as the expected
  PSP, MSP, CONTROL, and PRIMASK transfers.  Rizin and radare2 therefore remain
  useful boundary witnesses but are not semantic authorities for that class.
- The final-image whole-CFG PRIMASK pass classifies 102 reads.  Eighty-five
  reach a modeled register read: 81 disable-helper, two enable-helper, and two
  standalone cases.  Thirteen helper reads are overwritten, two stop at a
  call boundary, and two return.  Thirty read rows also carry the analyzer's
  `cycle` marker; its visited-node rule also marks CFG reconvergence, so that
  count does not prove 30 runtime loops.  The thirteen overwritten rows alone
  support the 52-byte ceiling.
- Kernel DWARF records GCC 16.2.0 with `-O`.  The generated PICO Makefile also
  defaults `COPTS?=-O`; size-flag and section-GC effects remain reserved for
  the constrained-C/build pass because the read-only boundary prohibits a
  comparison relink.
- Ghidra cache maintenance created or updated three observed ancillary files
  under `/var/tmp/eirikr-ghidra`: `.lastmaint` (46 bytes), `cache.map` (126
  bytes), and `db.1.gbf` (7012352 bytes).  The audit preserved them because
  their before-state is unknown.  Further Ghidra execution remains parked.
- The active audit ledger, scripts, indexes, and extracted artifacts reside
  under the audit scratch worktree (analyzers now under tools/analysis).  The pre-existing
  `/tmp/destdir-rp2040` staging directory and 2261-byte
  `/tmp/rp2040_build.log` remain as preserved residue; the read-only boundary
  does not authorize relocating or deleting them.

## Ranked next steps

The order uses a lexicographic gate.  Correctness and ABI violations outrank
every optimization.  Within optimization work, exact linked or architectural
evidence outranks source projections and raw candidate totals.  Bytes, cycles,
latency, flash wear, and SRAM-bank pressure remain separate columns; the audit
does not add them into a synthetic score or sum overlapping byte candidates.

Four ROM-divider admission gates and one benchmark-publication gate precede
the associated optimization claims:

1. Rebuild or explicitly retire PICO_UART.  Commit `0a09de3f` and the current
   PICO artifact restore divider state before application `r10`; PICO_UART
   still dereferences incoming `r10 + 40` as the saved context address.
2. Establish complete kernel ownership of divider transactions across IRQ,
   signal, and scheduling paths.  Userland runs with `CONTROL[0]=1`, so a
   userland shim cannot mask interrupts or write PRIMASK.  Fork, exec, exit,
   nested-use, and DIRTY/READY behavior need explicit coverage as well.
3. Replace `divrace` with a forced, observed collision oracle and negative
   control.  Replace `fptest` tolerances with independent raw-bit and ULP
   oracles over the declared numerical contract.
4. Repair the native AEABI entry seam and declare the ROM arithmetic numerical
   contract.  The board compiler cannot select the landed `__wrap___aeabi_*`
   symbols, while direct ROM calls alter NaN and subnormal behavior and retain
   strong double-fallback roots.
5. Repair CoreMark's integer report before publishing comparable scores.
   `HAS_FLOAT=0` omits the canonical score line and truncates seconds before
   the rate division; paired binary, root-block, fault-frame, stack-watermark,
   and serial evidence must replace commit-message-only provenance.

<!-- markdownlint-disable MD013 -->

| Order | Mechanism-first action | Evidence dimension | Primary authority | Completion gate |
| ---: | --- | --- | --- | --- |
| 1 | Separate the signal context, PendSV exception frame, and handler stack | Correctness: overlapping writes corrupt saved process state | ARMv6-M exception entry/return frame; `sig_machdep.c`; named-path disassembly | Compiled byte-range oracle plus hardware signal delivery and `sigreturn` preserve every register, mask, xPSR, alignment bit, and stack byte |
| 2 | Align every Smaller-C call boundary to AAPCS32 | Correctness: generated five-argument calls enter with `SP mod 8 = 4` | AAPCS32 public-interface stack rule; `cgthumb.c`; generated `thumb-native.s` | Assembly oracle over direct, indirect, variadic, helper, and structure calls plus differential execution |
| 3 | Move exception entry into explicit assembly or a proven naked ABI | Correctness: ordinary C prologues may precede architectural entry code | ARMv6-M exception-entry contract; linked first-instruction patterns | Supported compiler/flag matrix emits a fixed entry sequence and nested exceptions retain frames |
| 4 | Replace the underflowing SVC priority mapping | Correctness: linked SVC priority `0x80` contradicts the highest-priority source contract | ARMv6-M configurable priority rules; `intr.h`; linked startup stores | Exhaustive IPL map remains in range and an exception-ordering test matches the declared order |
| 5 | Establish zero-length copy semantics before end-address arithmetic | Correctness: two boundary underflows | C pointer-arithmetic bounds; `copyin`/`copyout`; Semgrep | Native tests cover zero, one, maximum, and wrapping ranges through both real seams |
| 6 | Return from `bzero` before its alignment prefix at length zero | Correctness: unaligned zero-length calls mutate canaries | C library zero-length contract; linked `bzero`; Semgrep | Alignment matrix preserves every canary for length zero and covers word-tail boundaries |
| 7 | Implement the RP2040-E15 Bulk-IN frame guard | Correctness: controller hang on affected VL805 paths; approximately 20 percent workaround bandwidth cost | RP2040 datasheet erratum E15; SDK 1.5+/TinyUSB SOF workaround | Controller-state test plus high-volume affected-B2/VL805 transfer test shows progress and quantified throughput |
| 8 | Define and implement the panic-path interrupt policy | Correctness: two disables plus a `nop` open zero interrupt window | `kern_synch.c`; ARMv6-M PRIMASK model; linked path | Permitted IRQ runs inside a bounded window while forbidden IRQs remain pending, or the source contract explicitly keeps all masked |
| 9 | Make the USB IRQ diagnostic type-correct | Correctness: signed `int` reaches `%u` | C variadic-format contract; compiler/cppcheck diagnostic | Target build accepts the exact call under format warnings as errors |
| 10 | Reconcile operator login and web-listener documentation | Operational correctness: six surfaces describe retired access/bind behavior | Board observations, account files, `login`, `su`, and host-script parser | All copies state operator then wheel `su`; listener prose matches loopback/token enforcement |
| 11 | Repair CoreMark's integer timing and score report | Benchmark correctness: the current path omits `CoreMark 1.0` and truncates fractional seconds before rate division | EEMBC `core_main.c`; `core_portme.c`; current OMAGIC formats | Non-float reporting preserves the declared precision and contract; paired images, root counts, fault frame, stack high-water mark, and serial transcripts support every size and runtime claim |
| 12 | Clear startup memory ranges exactly once | Cycles/boot latency: 108008 duplicate BSS bytes plus one overwritten/restored data range | Linker symbols; reset disassembly; Boot ROM Table 165 for optional bulk routines | Write-range trace has one initialization write per required byte and hardware cold boot preserves data/BSS |
| 13 | Replace the generated `resume` exchange expansion with a proved loop | Exact linked code surface: 3072 expanded bytes inside a 3176-byte symbol | `locore.S`; GNU/LLVM/Capstone named-path bytes | Static exchange/ABI equivalence plus process-switch stress and exact relink delta |
| 14 | Remove helper-local PRIMASK `ISB` instructions after architecture review | Exact linked surface: 844 bytes and 211 pipeline flushes | Pico SDK 2.3.0/CMSIS sequences; ARMv6-M synchronization rules | Instruction assertions retain six separately required barriers and interrupt-state stress passes |
| 15 | Batch UART TX to the programmed FIFO watermark | Interrupt latency/cycles: theoretical 11520 toward about 411 TX IRQ/s at 115200 baud | RP2040 datasheet sections 4.2.2.4 and 4.2.6.3; UARTIFLS Table 434 | Measured IRQ count, throughput, latency, stop/start, flush, wakeup, and console tests meet declared bounds |
| 16 | Place the USB TX ring in SRAM4 and SRAM5 | SRAM-bank pressure: 8192 bytes leave striped main SRAM and occupy both scratch banks | RP2040 SRAM map; linked `usbd.tx_ring` symbol | Link-map/DMA address assertions and USB saturation show correct access and improved contention |
| 17 | Replace eligible MMIO read-modify-write sequences with atomic aliases | Bus transactions and concurrency: 12 sites; 20-byte startup ceiling | RP2040 datasheet section 2.1.2; SDK `address_mapped.h` | Relinked sites use supported aliases, preserve reserved bits, and pass foreground/IRQ concurrency stress |
| 18 | Write `PENDSVSET` directly instead of reading ICSR first | Bus transaction/latency: one unnecessary PPB read and coupled status write | ARMv6-M ICSR write-one-to-set semantics | Linked SVC sequence contains the direct write and SVC-to-PendSV behavior remains correct |
| 19 | Add void interrupt-mask helpers only at thirteen proved dead-save sites | Exact linked ceiling: 52 bytes | Whole-CFG PRIMASK liveness; GNU/LLVM/DWARF witnesses | Every transformed destination is overwritten before read and nested mask restoration passes |
| 20 | Relax 48 mapping-proved Thumb branch pairs | Exact instruction ceiling: 96 bytes before layout | ARMv6-M conditional branch range; fixed-point linked layout model | Final displacement remains encodable, two-way CFG is identical, and exact section/file deltas are measured |
| 21 | Recompile libc `findiop.c` with `NSTATIC=8`, then relink the twenty-one source-proved payers | Source-object candidate: 5292 bytes across current proven manifest payers | `stdio.h`, `findiop.c`, OMAGIC headers, manifest | Per-image text/data/BSS and root-block deltas are measured; ninth-stream allocation/failure tests pass |
| 22 | Remove printf-float roots from the twenty source-closed programs | Raw retained-symbol candidate: 15936 bytes in twelve measured ELFs; eight more extents unknown | Makefile flags, `doprnt.c`, source format closure, ELF/a.out equality | `picoc` and `tclsh` are resolved separately; each relink measures archive/support/data/file-block effects and behavior |
| 23 | Enable section garbage collection only after resolving indirect roots and input-section ownership | Raw kernel symbol candidate: 24 groups and 2218 bytes; 85 indirect sites remain | ELF symbols/relocations/mapping markers; section-GC analyzer | Every indirect target, selected input section, alias, relocation, assembly-section boundary, and linker root is closed; then a function-section relink boots and passes subsystem tests |
| 24 | Rebuild or retire PICO_UART, replace the collision oracle, and establish kernel-owned divider transactions | Correctness blocker: committed source and PICO fix the pointer order, while PICO_UART retains incoming `r10 + 40`; current tests force no observed mid-ROM switch; `label_t` expansion adds 656 linked text bytes; the 304-byte integer-helper recovery remains potential | RP2040 section 2.3.1.5; SDK `hardware/divider.h`; CMSIS privilege contract; linked `longjmp`/`resume`; harness disassembly | Every target restores through a proved context pointer; a forced negative control fails; kernel preserves every divider value and CSR transition across IRQ, signal, scheduling, fork, exec, exit, and nested use; user mode performs no privileged masking |
| 25 | Repair the native AEABI entry seam and qualify the landed ROM-float implementation | Correctness and footprint blocker: native a.out links cannot select the wrappers; one 432-byte section strongly roots four double fallbacks with 5300 raw text bytes before dependencies; numerical semantics differ | RP2040 section 2.8.3.2; SDK float/double wrappers; native `cc`/`ld`; ELF relocations | Direct AEABI symbols or proved native wrap semantics work in real board programs; V1/V2 and IEEE/AEABI differential matrices pass; exact linked bytes and cycles beat retained software paths |
| 26 | Apply Boot ROM copy/fill only to aligned, nonoverlapping hot paths | Bytes/cycles unknown | RP2040 section 2.8.3.1.2, Table 165 | Lookup/version/alignment/size matrix passes and a relink plus cycle benchmark shows a net win |
| 27 | Use XIP hit/access counters to choose scarce SRAM placement | Measurement prerequisite; zero current recovery claim | RP2040 section 2.6.3.6, Tables 158-159 | Representative counter deltas are repeatable and placement assertions bind only measured hot paths |
| 28 | Allocate SwapRAM from counted compressed size | Capacity/latency: recover otherwise rejected compressible admissions; bytes workload-dependent | Current heatshrink implementation and pool allocator | Count and encode lengths agree over corpus, exact-capacity/fragmentation cases pass, and hardware fork/restore stress succeeds |
| 29 | Specify and then coalesce raw-swap partial writes | Flash wear/latency: each partial write triggers a 4096-byte read-modify-erase-program cycle | Flash implementation and erase/program contract | Trace proves fewer erase/program operations while power-loss/fault injection preserves the selected consistency model |
| 30 | Stop treating disconnected OMAGIC symbols as free bytes | Prevented mis-optimization: 6624-byte top-five set yields zero proved discardable bytes | OMAGIC header parser, numeric branch containment, archive provenance | Reopen only after selected-member and complete indirect/stored-pointer evidence isolates a discardable input section |

<!-- markdownlint-enable MD013 -->

The manual-page feature remains feasible only with the measured 42728-byte
archive, integrity metadata, a userland decoder or explicit syscall, and a
correct board-specific `cc` page.  The integer MLP remains a research program
until a deterministic trainer/exporter, licensed corpus allowlist, quantized
parity, accuracy, saturation, resident-footprint, and board-latency evidence
exist.  Neither deferred feature outranks the correctness or measured native
work above.

## Residual uncertainties and next evidence gates

- The retained baseline root predates CoreMark admission, while the final
  generated root contains the exact current CoreMark and reports 113 free
  blocks.  Baseline recovery sums and current-root totals therefore remain
  distinct evidence sets.  Post-flash behavior still needs attended
  revalidation.
- The CoreMark `HAS_FLOAT=0` and Makefile edits are bound to one rebuilt
  integer-format image and the final root/UF2 chain, but not to a hashed
  pre-change image, fault frame, stack high-water trace, or serial transcript.
  The float-formatter overflow and 7720-byte recovery remain candidate claims.
  The current source independently proves two reporting gaps: the canonical
  score line is omitted and integer seconds truncate before rate calculation.
- A temporary working manifest admitted `fptest` and `divrace`; the current
  manifest matches `HEAD` and omits both.  An exact manifest-to-root build is
  required before either program enters a footprint denominator.
- The retained `divrace` loop forces no scheduling event or observed divider
  collision, and both retained harnesses use self-referential tolerance
  oracles.  Their board passes remain narrow observations rather than release
  admission evidence.
- `picoc` dynamic formats and libtcl's `tclsh` closure remain the two printf
  unknowns.
- The 85 kernel indirect-control sites and 4532 unknown OMAGIC bytes bar
  garbage-collection claims.
- The instruction histogram, branch-relaxation set, PRIMASK liveness totals,
  and section-GC set have been replayed against PICO `e69a97e3...`.  The wider
  named-path and emulation addresses remain bound to the retained
  `c9feb49b...` baseline; only the current divider path has a new bounded
  address-level cross-check.
- Boot ROM float net bytes, wrapper ownership, Boot ROM V1 coverage, and exact
  workload cycles remain unmeasured.
- The board must supply attended correctness, timing, IRQ-count, flash-write,
  and SRAM-contention evidence in a separately authorized pass.
- The concurrent final flash invalidates temporal attribution of the earlier
  hardware observations to the bytes now on the board.  Post-flash login,
  SwapRAM, shell/editor, ROM-float, and divider tests remain unobserved by this
  audit.
- Repository documentation still contains the core-local-divider wording and
  login/access drift; the read-only boundary leaves those source files
  unchanged.
