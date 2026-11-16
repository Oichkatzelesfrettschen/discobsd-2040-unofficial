# RP2040 Compiler and Kernel-Size Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce warning-free GCC and Clang RP2040 builds across eight optimization levels, measure and lawfully repair the `-O0` and `-O3` flash deficits, retain useful debug evidence, and close the final aggregate without changing the fixed flash layout.

**Architecture:** The implementation separates compiler ABI correctness, physical load-span admission, source-level size control, retained-root safety, debug companions, runtime resource instrumentation, and packed/raw execution measurement.  Each mechanism receives a calibrated positive and negative control before promotion; final claims bind one staged index to immutable build and hardware evidence.

**Tech Stack:** C17, ARMv6-M Thumb-1 assembly, bmake, GNU GCC/binutils, Clang, a.out/ELF tooling, cflow, cscope, Python gate scripts invoked through `${PYTHON}`, Renode, and RP2040 board probes.

**Spec:** `docs/research/rp2040-compiler-size-compatibility-scope.md`

## Global Constraints

- Preserve the 128 KiB boot2-plus-kernel partition, 1,536 KiB Dhara root, 384 KiB raw swap, and 192 KiB process window.
- Test `-O0`, `-O`, `-O1`, `-O2`, `-O3`, `-Og`, `-Os`, and `-Oz` with system ARM GCC 16.2.0, Arm GNU GCC 13.2.1, and Clang 23.1.4 targeting ARM.
- Keep `-Wall -Wextra -Werror -Wshadow -Wundef -Wformat=2`; calibrate every added or compiler-specific warning flag.
- Preserve configured syscalls, diagnostics, storage, console, packed exec, debugger roots, and failure semantics.
- Use repo-relative durable paths and `${PYTHON}`; retain exact tool versions, commands, hashes, maps, objects, and receipts.
- Treat host, qemu-user, Renode, and physical-board results as separate evidence classes.
- Run `bmake MACHINE=rp2040 logged-build` before the final `logged-check`; any later edit reopens the affected gates.
- Before its owning commit, every introduced gate must name its executable command and add its root or test Makefile target, TESTING contract, execution-inventory row, applicable lint ownership, and CI owner; a hardware-only gate retains explicit opt-in and remains outside the non-hardware aggregate.

## Review Focus

- A `.ramfunc` initializer crosses the flash boundary while ordinary `.text` fits; the load-span gate must reject it.
- A controller probe enters a device slot or a device probe enters a controller slot; compilation must reject the mismatch.
- A retained indirect-root name points at the wrong address; the root gate must compare values, not names alone.
- Packed preflight fails after consuming input; the old process image must remain intact and commit must remain unentered.
- A size-saving helper changes interrupt-stack depth or volatile I/O order; stack and behavioral probes must reject the regression.

---

### Task 1: Freeze final-candidate identity and evidence manifest

**Files:**

- Create: `tools/capture_compiler_matrix.py`
- Create: `tests/compiler_matrix/check.py`
- Modify: `Makefile`
- Modify: `sys/arch/rp2040/doc/TESTING.md`
- Modify: `tools/test-execution-inventory.json`
- Modify: `.github/workflows/firmware.yml`
- Modify: `tools/check_build_observability.py`

**Interfaces:**

- Produces: `capture_compiler_matrix.py --manifest PATH --output DIR`, with compiled-input closure hash, index-entry hash, HEAD, generated-input hashes, tool versions, runtime-archive hash, effective commands, and per-row terminal state.

- [ ] Write positive and negative manifest fixtures; require changed index entries, unstaged source mutation, generated-input drift, and missing host tools to fail before compilation.
- [ ] Run `${PYTHON} tests/compiler_matrix/check.py` and verify the missing identity checks fail.
- [ ] Implement the capture driver with clean-per-row execution, `bmake -k`, before/after compiled-input hashes, immutable row directories, prerequisite checks, and failed-link artifact retention.
- [ ] Identify the Pico SDK checkout and compiler from their actual paths and version output; record an unavailable SDK compiler as an open matrix row rather than treating Arm GNU 13.2.1 as the SDK.
- [ ] Run each mutating row through the logged product owner or a disjoint staged-index reconstruction; calibrate active-owner refusal and bounded cleanup.
- [ ] Add the cheap `check-compiler-matrix-harness` self-test to root, execution-inventory, lint, and CI ownership without placing the full 24-row sweep recursively inside `logged-check`.
- [ ] Run the calibrated fixture and commit as `build: bind compiler matrices to candidate identity`.

### Task 2: Review the existing staged semantic candidate

**Files:**

- Create: `docs/research/rp2040-compiler-staged-review.md`
- Review: every staged file recorded by the Task 1 manifest
- Review: focused receipts and calibrated tests named by each staged mechanism

**Interfaces:**

- Produces: a complete finding ledger with staged locator, mechanism, evidence, severity, remedy, owning regression, and disposition before new production edits begin.

- [ ] Partition the staged diff by build stamping, formatter, checkpoint state, capacity admission, readlink publication, fault literals, copied init arguments, typed callbacks, pipe I/O, warnings, metadata, debug links, tests, ledgers, inventories, and documentation.
- [ ] Review every removed line and every semantic change against callers, generated consumers, ABI layout, error paths, side-effect order, and its claimed regression.
- [ ] Run each existing focused test and its known-bad mutation; record unavailable or mismatched oracles as open findings rather than passes.
- [ ] Reconcile C17 ledger, source denominator, execution inventory, generated Makefiles, TESTING labels, and survey evidence with the exact staged source.
- [ ] Resolve every grounded FATAL or SERIOUS finding in an independently testable commit before the compiler/size changes consume the candidate.
- [ ] Commit the final review ledger as `docs: reconcile staged compiler candidate semantics`.

### Task 3: Make graph capture complete and reproducible

**Files:**

- Create: `tools/capture_rp2040_callgraph.py`
- Create: `tools/callgraph-indirect-edges.json`
- Create: `tests/callgraph/check.py`
- Create: `docs/research/rp2040-callgraph-manifest.md`
- Modify: `sys/arch/rp2040/doc/TESTING.md`
- Modify: `Makefile`
- Modify: `tools/test-execution-inventory.json`
- Modify: `.github/workflows/firmware.yml`

**Interfaces:**

- Consumes: PICO `CFILES`, generated `ioconf.c`, linker vectors, callback tables, and map/relocation files.
- Produces: source denominator, cscope database manifest, cflow graph, indirect-edge manifest, `-Os` symbol and failed `-O0`/`-O3` object/section overlays, tool versions, diagnostics, commands, source identity, and repo-relative retained-evidence hashes.

- [ ] Add a positive edge fixture and mutations for a missing edge, wrong callback address, and missing generated source.
- [ ] Run `${PYTHON} tests/callgraph/check.py` and require every mutation to fail.
- [ ] Implement configuration-aware source enumeration and static graph capture; record cflow duplicate-definition and indirect-call limits.
- [ ] Populate vectors, syscalls, driver/service/fileops/timeout/exec/nlist/assembly/ramfunc edge families with exact source locators and expected types.
- [ ] Overlay direct and indirect nodes with exact linked symbols where available, exact failed-map object/section contributions, and explicitly labeled map-only upper bounds; omit scratch absolute paths from the durable manifest.
- [ ] Run the gate on PICO and commit as `tools: capture RP2040 direct and indirect call graphs`.

### Task 4: Split controller and device callback ABIs

**Files:**

- Modify: `sys/sys/config.h`
- Modify: `tools/config/mkioconf.c`
- Modify: `sys/arch/rp2040/rp2040/machdep.c`
- Modify: `sys/arch/stm32/stm32/machdep.c`
- Modify: affected RP2040 and STM32 driver declarations and generated configuration files
- Create: `tests/kernel/driver_callback_contract.c`

**Interfaces:**

- Produces: `struct controller_driver` with `int (*d_init)(struct conf_ctlr *)` and `struct driver` with `int (*d_init)(struct conf_device *)`.

- [ ] Write compile-negative fixtures that cross-install the two probe types and a behavioral fixture that records order, pointer identity, return value, and alive transitions.
- [ ] Verify the old header accepts both cross-category installations or lacks the required type contract; record that failure of enforcement as the pre-repair result.
- [ ] Implement the two descriptor types and update generator output, RP2040 entries, STM32 SPI/SDIO controller entries, and device entries without function-pointer casts.
- [ ] Regenerate all four RP2040 configurations and one nonempty STM32 controller/device configuration.
- [ ] Require both wrong-category fixtures to produce the expected incompatible-type diagnostic under GCC and Clang; weaken the header deliberately and require the gate to fail.
- [ ] Run mismatch, dispatch, generated-sync, and both-compiler tests; commit as `kernel: separate controller and device probe types`.

### Task 5: Resolve the service callback contract

**Files:**

- Modify: `sys/sys/config.h`
- Modify: `tools/config/mkioconf.c`
- Modify: `sys/kern/init_main.c`
- Create: `tests/kernel/service_callback_contract.c`
- Modify: service documentation discovered by the compatibility search

**Interfaces:**

- Produces: one documented service attachment signature shared by generator declarations and startup dispatch.

- [ ] Inventory all in-tree, historical, STM32, and generated service callbacks and record whether any integer has defined semantics.
- [ ] Stop for an architecture decision if a live integer consumer exists; otherwise select `void (*)(void)` to match the implemented zero-argument dispatch.
- [ ] Add a live generated positive callback and a wrong-arity negative callback; require the negative compile to fail.
- [ ] Update declaration, generation, invocation, and documentation together; run generated and behavior gates.
- [ ] Commit as `kernel: define the service attachment ABI`.

### Task 6: Type and verify privileged `ucall` dispatch

**Files:**

- Modify: `sys/arch/rp2040/rp2040/sysctl.c`
- Modify: `sys/arch/stm32/stm32/sysctl.c`
- Modify: `sys/arch/rp2040/include/io.h`
- Modify: `sys/arch/stm32/include/io.h`
- Modify: `share/man/man2/ucall.2`
- Create: `tests/kernel/ucall_contract.c`

**Interfaces:**

- Produces: a kernel routine type `int (*)(int, int)` and a source-compatible public declaration whose supported callee signature is explicit.

- [ ] Add tests for asymmetric signed arguments, return bits, zero/nonzero priority selection, denial before invocation, and exact saved-priority restoration; add wrong-arity compile rejection.
- [ ] Run the fixture against current unprototyped dispatch and confirm the compile-negative oracle detects the missing contract.
- [ ] Type both architecture records and update interface documentation without changing syscall record field widths or order.
- [ ] Inspect ARM disassembly for r0/r1 arguments and r0 result; run host GCC and Clang fixtures.
- [ ] Commit as `kernel: type privileged two-argument calls`.

### Task 7: Close Clang compilation and link compatibility

**Files:**

- Modify: `sys/arch/rp2040/conf/Makefile.rp2040`
- Modify: four generated RP2040 kernel Makefiles
- Modify: source files identified by fresh Clang diagnostics
- Modify: `tests/compiler_matrix/check.py`

**Interfaces:**

- Consumes: Tasks 4--6 callback contracts.
- Produces: complete Clang objects, link map, ELF/UF2 where size permits, and an explicit GCC-runtime compatibility receipt.

- [ ] Run all eight Clang rows with `-ferror-limit=0` and group diagnostics by root cause rather than line count.
- [ ] Add a calibrated regression for each newly exposed diagnostic class before changing production source.
- [ ] Repair the source or build interface without casts, warning suppressions, or semantic fallback; synchronize generated Makefiles.
- [ ] Verify Clang target attributes, EABI tags, relocations, helper symbols, and selected GCC ARMv6-M soft-float `libgcc.a` members.
- [ ] Compare `-mcpu=cortex-m0plus` with supported `-march=armv6-m -mtune=cortex-m0plus` forms through predefined macros, ELF attributes, disassembly, ABI tags, helper calls, and linked bytes; record compiler-specific rejection instead of suppressing it.
- [ ] Rerun all Clang rows and commit as `rp2040: close Clang ARM kernel compatibility`.

### Task 8: Extend full physical flash-load admission

**Files:**

- Modify: `tools/check_kernel_headroom.py`
- Modify: `sys/arch/rp2040/conf/kern.ldscript`
- Modify: `Makefile`
- Modify: `sys/arch/rp2040/doc/TESTING.md`
- Modify: `.github/workflows/firmware.yml`
- Modify: `tools/check_build_observability.py`

**Interfaces:**

- Produces: the existing `check_kernel_headroom.py` ELF admission with overlap validation and an analysis-only failed-map mode; release admission retains the existing 1,024-byte default margin.

- [ ] Extend the existing 35-case self-test with exact-boundary, one-byte-over, `.ramfunc`-only-over, large-DWARF, NOBITS, and malformed-overlap fixtures.
- [ ] Confirm the current linker-message oracle wrongly accepts or undercounts the `.ramfunc` negative control.
- [ ] Implement interval validation, a snapshot-labeled analysis mode for failed maps, and an explicit final `.ramfunc` load-end assertion in `kern.ldscript`.
- [ ] Use `--margin 0` for geometric exact-fit calibration and the default 1,024-byte margin for release admission; keep these verdicts separate.
- [ ] Run every fixture plus retained maps and the fresh `-Os` ELF; require retained GCC16 excesses 21,936/5,868 and fresh GCC16 excesses 21,968/5,900 bytes under their named source snapshots.
- [ ] Commit as `rp2040: bound the complete kernel flash load span`.

### Task 9: Establish the primary-source measurement contract

**Files:**

- Modify: `docs/research/rp2040-compiler-size-compatibility-scope.md`
- Create: `tests/compiler_matrix/target_flags.c`
- Create: `tests/rp2040/resource_contract.json`
- Create: `tests/rp2040/check_resource_contract.py`
- Modify: `sys/arch/rp2040/doc/TESTING.md`

**Interfaces:**

- Produces: versioned source locators, target-flag probes, register ownership, measurement units, repetition rules, unchanged-control baselines, and numeric promotion budgets consumed by Tasks 10--11 and 14--15.

- [ ] Record exact editions and sections for ARMv6-M encodings, AAPCS32, RP2040 XIP/cache/bus/timer registers, GCC/Clang options, GNU ld load addresses, ELF program headers, and DWARF non-ALLOC sections.
- [ ] Compile the target-flag probe under every compiler spelling and require the expected macros, EABI attributes, Thumb-1 instruction set, soft-float ABI, and Cortex-M0+ tuning disposition.
- [ ] Define counter clear/start/stop order, owner, unit, timer resolution, saturation, interrupt and second-core treatment, warm/cold cache controls, probe overhead, repetitions, and uncertainty calculation.
- [ ] Measure unchanged controls and freeze numeric budgets for worst-case stack reserve, peak RAM, flash traffic, and workload latency before opening source trials; require at least 1,024 bytes of flash headroom.
- [ ] Calibrate the contract gate with wrong units, stale baselines, zero-moving counters under known work, and a post-result budget change; require every mutation to fail.
- [ ] Commit as `tests: define RP2040 optimization measurement contracts`.

### Task 10: Attribute default-COPTS `-O0` growth and factor sources

**Files:**

- Modify: source units selected by retained object and symbol evidence, initially `sys/kern/ufs_namei.c`, `sys/arch/rp2040/dev/usb.c`, `sys/arch/rp2040/dhara/journal.c`, `sys/kern/ufs_syscalls.c`, and `sys/kern/tty.c`
- Create: focused contract tests beside each selected subsystem
- Extend: `tools/capture_compiler_matrix.py`

**Interfaces:**

- Produces: one-variable source patches with linked-load delta, local symbols, literal pools, stack use, and behavior results.

- [ ] Retain all failed-link objects, symbols, relocations, disassembly, literal mapping symbols, compiler dumps, stack-use files, and every effective per-object option for unchanged-source `-O0` and `-Os` controls; record the mandatory `kern_time.c -Os` exception.
- [ ] Rank exact object growth and exact available function sizes; label map-only spans as upper bounds.
- [ ] For each selected function, write error-order, side-effect, and boundary tests before factoring repeated control flow or constants into typed helpers.
- [ ] Accept a factoring patch only when its declared linked-byte threshold, behavior tests, and stack bound pass; revert rejected trials cleanly.
- [ ] Commit each accepted subsystem independently with its measured delta; implementation remains incomplete until the default-COPTS `-O0` profile fits with 1,024-byte headroom.

### Task 11: Control `-O3` duplication narrowly

**Files:**

- Modify: measured formatter, TTY, signal, exec, copy, or sampler sources selected by one-variable trials
- Extend: focused subsystem tests and matrix capture

**Interfaces:**

- Produces: the smallest source-annotation/factoring group that fits `-O3` with required headroom and passes runtime/stack contracts.

- [ ] Reproduce the unmodified 136,940-byte full span and the retained 128,976-byte combined trial on one frozen version payload.
- [ ] Replay each no-inline or factoring change independently; capture inline reports, linked delta, stack delta, and workload latency.
- [ ] Add differential tests for format destinations, TTY ordering, signal delivery, exec spool failures, overlapping copy contracts, and volatile sampler reads.
- [ ] Select the minimal Pareto set that provides at least 1,024 bytes of headroom without a correctness or declared latency/stack regression.
- [ ] Commit independently reviewable mechanisms, then rerun `-Os`, `-Oz`, and `-O0` to expose cross-level costs.

### Task 12: Decide section collection from a complete root audit

**Files:**

- Modify: `sys/arch/rp2040/conf/Makefile.rp2040` only if accepted
- Modify: `sys/arch/rp2040/conf/kern.ldscript` only for justified `KEEP` roots
- Extend: `tools/callgraph-indirect-edges.json` and `tests/callgraph/check.py`

**Interfaces:**

- Consumes: Task 3 edge manifest and Task 8 load-span verifier.
- Produces: a discarded-section ledger with semantic owner and root decision for every discarded section.

- [ ] Build split/no-GC, unsplit/GC, and split/GC controls on identical source and version metadata.
- [ ] Mutate one vector, syscall pointer, file operation, timeout, device switch, nlist address, and `.ramfunc` dependency; require each root oracle to fail.
- [ ] Audit every discarded section and reject collection if any edge family remains unenumerated or a pointer-value mutation passes.
- [ ] Promote function/data sections and collection only if the complete audit and all runtime gates pass; otherwise record the measured rejection.
- [ ] Commit accepted linker policy separately as `rp2040: collect proven unreachable kernel sections`.

### Task 13: Define debug profiles and native debug companions

**Files:**

- Create: `tools/check_debug_companions.py`
- Create: `tests/debug_companions/check.py`
- Modify: native a.out library recipes and debug documentation selected by the capture

**Interfaces:**

- Produces: `default-copts-O0`, optional `all-object-O0`, and `debuggable` profile names, retained host ELF/object companions, and a source/text/relocation binding to shipped a.out objects.

- [ ] Inventory every shipped C and assembly artifact's effective final optimization and debug flags from a clean captured world.
- [ ] Define debugger-fidelity tests for line mapping, variables, backtraces, inline frames, relocations, and stripped load-byte identity.
- [ ] Calibrate a companion mismatch by changing source or one relocation and require binding failure.
- [ ] Implement companions only where source, `.text`, relocations, and final addresses bind exactly; keep target archives compact.
- [ ] Compare fitting `-Og`/`-O` debuggable profiles with the default-COPTS `-O0` profile explicitly; measure an all-object `-O0` variant separately if required, then commit as `build: retain bound debug companions for RP2040 artifacts`.

### Task 14: Instrument memory, storage, CPU, cache, and flash costs

**Files:**

- Create: `tests/rp2040/resource_probe.c`
- Create: `tools/summarize_rp2040_resources.py`
- Modify: `sys/arch/rp2040/doc/TESTING.md`
- Modify: `Makefile`
- Modify: `tools/test-execution-inventory.json`
- Modify: `.github/workflows/firmware.yml`
- Temporarily modify and later revert: `distrib/rp2040/mi.rp2040`

**Interfaces:**

- Consumes: the frozen units, ownership, controls, repetitions, uncertainty method, and numeric budgets from Task 9.
- Produces: greppable records for load span, kernel RAM, process admission, stack/heap high-water, root blocks, cycles, XIP/cache counters, and flash program/erase counts.

- [ ] Implement the Task 9 counter ownership, clear/start/stop, saturation, overhead, workload, and sampling contracts without changing their post-control budgets.
- [ ] Add gate-off and known-work controls; reject zero or unchanged counters when known work should move them.
- [ ] Implement bounded stack/heap watermarks and snapshot-only statistics behind the test-image configuration.
- [ ] Run host/qemu-user instruction checks before any board action; stage the probe only in a named test image.
- [ ] Document measurement uncertainty and revert test-image manifest entries before the production commit.

### Task 15: Attribute packed versus raw exec phases

**Files:**

- Modify: `sys/arch/rp2040/rp2040/exec_hsaout.c` behind a test instrumentation option
- Modify: `sys/arch/rp2040/rp2040/hsx_stream.c` behind the same option
- Create: `tests/rp2040/exec_resource_probe.c`
- Extend: `tools/summarize_rp2040_resources.py`
- Modify: `Makefile`
- Modify: `tools/test-execution-inventory.json`
- Modify: `.github/workflows/firmware.yml`

**Interfaces:**

- Produces: separate preflight and commit records for bytes, decoder transitions, CRC work, `rdwri` calls/residuals, cycles, counters, and stack.

- [ ] Add host decoder controls for valid input, truncated input, CRC failure, output overflow, and commit suppression after preflight failure.
- [ ] Measure probe overhead with instrumentation disabled and enabled on identical raw work.
- [ ] Implement phase-tagged counters without allocating from the process window or changing normal control flow when disabled.
- [ ] Verify the two-pass packed invariant and reject any trial that removes preflight or changes old-image survival.
- [ ] Commit test instrumentation and its calibrated gate separately from performance conclusions.

### Task 16: Run the final eight-level, three-compiler matrix

**Files:**

- Update: `docs/research/rp2040-compiler-debug-optimization.md`
- Update: `docs/research/rp2040-compiler-size-compatibility-scope.md`

**Interfaces:**

- Consumes: Tasks 1--15.
- Produces: 24 clean row receipts plus host GCC/Clang fixture receipts and one source-matched comparison table.

- [ ] Freeze the compiled-input closure, verify tracked worktree bytes match the staged index, and record deterministic version payload, generated inputs, compiler/linker/runtime hashes, configuration, and host-tool hashes before and after every row.
- [ ] Run each row from clean with `bmake -k`; retain objects and diagnostics even on failure.
- [ ] Run load-span, root, debug, and stack summarizers for every row and classify fit independently of warning-free compilation.
- [ ] Compare `-O` with `-O1` and `-Os` with `-Oz` by effective optimizer flags, load bytes, and disassembly rather than section size alone.
- [ ] Update the survey with only final-snapshot results; preserve older tables as explicitly historical evidence.

### Task 17: Run focused gates and the fresh aggregate

**Files:**

- Modify only defects revealed by the gates, with their owning regression in the same commit.

**Interfaces:**

- Produces: final `logged-build` and `logged-check` receipts bound to the same compiled-input closure as Task 16.

- [ ] Run warning, ABI, graph, load-span, root, debug, packed-exec, C17 ledger, generated-sync, execution-inventory, lint, host, cross, qemu-user, and board-build focused gates.
- [ ] Diagnose every failure to its root cause; add or strengthen the calibrated regression before repairing production code.
- [ ] Run `bmake MACHINE=rp2040 logged-build` with an exported nonempty `${PYTHON}` and require exit 0.
- [ ] Run `bmake MACHINE=rp2040 logged-check` and require build stage 0, check stage 0, and total exit 0.
- [ ] Recompute the compiled-input closure and staged-index identity; rerun affected gates and the aggregate after any edit.

### Task 18: Execute authorized board validation and restore plain image

**Files:**

- Update: named evidence records under the existing RP2040 documentation ownership
- Revert: every temporary test-image manifest entry

**Interfaces:**

- Consumes: exact final kernel/root images from Task 17 and existing user authorization for serial/flashing.
- Produces: normalized transcripts, device identity, boot IDs, hashes, workload results, and verified restoration.

- [ ] Resolve the board through `/dev/serial/by-id/*DiscoBSD*`, record before-state identity, and verify the selected final artifacts.
- [ ] Run gate-off/on packed/raw campaigns with bracketed controls, counter ownership, stack watermarks, and exact workload success checks.
- [ ] Run callback, process-1, filesystem, console, timer, swap, fault, and flash-XIP closure checks required by changed mechanisms.
- [ ] Flash the verified plain image, boot it, verify its hashes/release identity, and record the measured restored state.
- [ ] Remove temporary staging only after transcript and artifact retention succeeds.

### Task 19: Review, commit, integrate, and retire

**Files:**

- Update: `docs/INDEX.md`
- Update: final research and testing documents
- Review: complete staged diff and retained evidence manifest

**Interfaces:**

- Produces: bisectable commits, reviewed pull request, merge commit, and a clean retired worktree.

- [ ] Split shared ledgers, Makefiles, inventories, and TESTING hunks by owning mechanism; verify every commit builds and its focused test passes.
- [ ] Read `git diff --staged` adversarially for removals, label-command agreement, symbol resolution, secrets, local paths, and unrelated changes.
- [ ] Run an independent whole-branch adversarial review and resolve every grounded FATAL or SERIOUS finding with code/evidence or explicit scope correction.
- [ ] Push only to `origin`, open the focused PR, require green CI and review, and merge with a merge commit.
- [ ] Verify the clean worktree HEAD is reachable from the pushed merged ref, remove it without force, and confirm removal with `git worktree list --porcelain`.
