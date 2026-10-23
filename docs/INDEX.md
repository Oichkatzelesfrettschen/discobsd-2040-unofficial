# Documentation map

Every document in the tree, and what each one establishes. A reader who
knows the question but not the file starts here.

## Where a document lives

Two homes, one rule.

`sys/arch/rp2040/doc` holds what code, a Makefile or the root manifest
cites as the authority for a shipped mechanism. Change the mechanism and
the document changes with it; the citing artifact is named beside each
entry below.

`docs/research` holds the investigation: option surveys, size audits,
tuning reports, handbacks, and the reports behind decisions the code
already reflects. Nothing in the build reads these.

The sibling repository `discobsd-2040-notes` keeps the vendored RP2040 and
Pico datasheet PDFs under `docs/rp2040/`.
`sys/arch/rp2040/doc/DATASHEET-INDEX.md` resolves a section number to its
page in those files without carrying them here.

## Start here

| Question | Document |
| --- | --- |
| What is this and how do I run it? | `README.md` |
| How does the board get from reset to a shell prompt? | `sys/arch/rp2040/doc/BOOT-MAP.md` |
| How do I connect to the console? | `sys/arch/rp2040/doc/USER-ACCESS.md` |
| Which datasheet section covers this register? | `sys/arch/rp2040/doc/DATASHEET-INDEX.md` |
| What does each test gate prove, and what does it need? | `sys/arch/rp2040/doc/TESTING.md` |
| What does the MPU protect, and how is that proven? | `sys/arch/rp2040/doc/MPU.md` |
| What fixed-table and kernel-stack headroom has a workload used? | `sys/arch/rp2040/doc/CAPACITY.md` |
| Where do the 2 MB of flash go? | `sys/arch/rp2040/doc/STORAGE.md` |
| How do I build a smaller root, and what does each feature cost? | `sys/arch/rp2040/doc/PROFILES.md` |
| How do I work on this tree? | `AGENTS.md` (`.claude/CLAUDE.md` is its generated compatibility copy) |
| What license travels with an image I hand someone? | `NOTICE`, then `docs/research/legal-memo-redistribution.md` |
| How do I build and package the host tools? | `distrib/rp2040/host/DEVELOPMENT.md` |

## Port documentation: `sys/arch/rp2040/doc`

| Document | Establishes | Cited by |
| --- | --- | --- |
| `BOOT-MAP.md` | the path from the boot ROM through boot2, kernel entry and init to a login prompt | reader entry point |
| `CAPACITY.md` | compact kernel metadata, fixed-table and u-area counters, and the evidence required before a limit changes | the metadata generator, `sys/kern/subr_capacity.c`, `sbin/sysctl` |
| `DATASHEET-INDEX.md` | datasheet section to page number, for both the RP2040 and Pico documents | every hardware citation in the tree |
| `MPU.md` | the Cortex-M0+ MPU map the kernel programs, the registers it reads back, and the fault test that proves it | `sys/arch/rp2040/rp2040/mpu.c`, `usr.bin/mputest` |
| `MULTICALL-BSS-OVERLAY.md` | how applets in one multicall binary share a BSS lifetime | the multicall `*.c.in` generators |
| `PROFILES.md` | the root filesystem profiles, the measured block cost of each optional closure, and what the composition checker refuses | `distrib/rp2040/mi.rp2040`, `profiles`, `mkmanifest.py`, `Makefile.inc`, the root `Makefile` |
| `STORAGE.md` | the flash budget and the 128 KB / 1536 KB / 384 KB layout chosen | `distrib/rp2040/Makefile.inc` |
| `TESTING.md` | each gate, its tier, its prerequisites, and what a pass proves | the root `Makefile` and both CI workflows |
| `USER-ACCESS.md` | console access over CDC-ACM and UART0, and the login accounts | reader entry point |
| `dhara-geometry.txt` | the Dhara geometry of the root region: page, erase block, checkpoint group, garbage-collection ratio and capacity | `check-dhara-amplification`, `STORAGE.md` |

### Authorities for a shipped mechanism: `doc/research`

`doc/research/README.md` lists this set. Each is the authority a build
artifact points at.

| Document | Establishes | Cited by |
| --- | --- | --- |
| `audit-findings.md` | the read-only audit ledger, a verbatim capture with its source SHA-256 | immutable; `audit-response.md` answers it |
| `audit-response.md` | how each ranked audit item was resolved or deferred | `tests/rp2040/fptest/Makefile` |
| `benchmarks.md` | the CoreMark port and its reporting contract | `benchmarks/Makefile`, `benchmarks/coremark/Makefile` |
| `board-libc.md` | how the board-native link library is composed | `distrib/rp2040/mkboardlibc.py`, `boardlibc-members` |
| `emulation.md` | the Renode flow that boots the kernel off the real boot ROM | `tools/renode/boot.resc`, `boot.robot` |
| `float-libs.md` | the Boot ROM float provider contract, bit-exact | `tests/rp2040/fptest/fptest.c` |
| `games.md` | what `gamebox` carries and why | `distrib/rp2040/mi.rp2040` |
| `menu-shell.md` | the Rockbox-shaped menu shell and its line editing | `bin/sh/edit.c` |
| `posix-tools.md` | what `textbox` carries, from sbase | `sbin/textbox/`, `distrib/rp2040/mi.rp2040` |
| `rockbox-ui.md` | Rockbox measured as a UI source for this board | `usr.bin/menu/` |
| `sh-posix-audit.md` | the POSIX ledger for `bin/sh`, row by row | `bin/sh/tests/posix-sh.sh` tests its rows; its xfail cases pin the open items |
| `utilbox.md` | what `utilbox` and `adminbox` carry | `sbin/utilbox/`, `sbin/adminbox/` |
| `v7-tools.md` | the V6/V7 disk images, their licenses, and two ports | `sbin/textbox/Makefile`, `legacy/pdp11-v6/distrib/rp2040/mi.rp2040` |
| `zswap.md` | the compressed RAM tier in front of flash swap | `compile/PICO/Config`, `heatshrink/heatshrink_config.h` |

## Research corpus: `docs/research`

Carried with their history from the notes repository, and added to since.
Grouped by what they investigate.

### Hardware and the board

| Document | Establishes |
| --- | --- |
| `bootsel-button.md` | BOOTSEL and USB re-entry on bare metal, with no debug probe |
| `firmware-updates.md` | what firmware an original Pico (silicon B2) carries and what an owner can update |
| `uart-pl011.md` | the PL011 UART0 console driver and its Renode verification |
| `board-inventory-2026-09-15.txt` | the boards on hand and what each one is |

### USB console

| Document | Establishes |
| --- | --- |
| `usb-cdc-baremetal.md` | the bare-metal CDC-ACM device driver, the reference research behind `dev/usb.c` |
| `usb-outwedge.md` | the bulk-OUT wedge mechanism and how the driver recovers itself |
| `usb-outwedge-sim.py` | a host model of the wedge, executable |
| `usb-reopen.md` | the wedge a host reopen provokes |
| `usb-resilience.md` | connection handling and the tools that diagnose it |
| `console-silent-after-web-session.md` | why the console goes quiet after a web session |

### Memory, storage and size

| Document | Establishes |
| --- | --- |
| `memory-ownership-plan.md` | memory ownership chosen over instruction tuning, and what landed |
| `rp2040-memory-wear-engineering-program.md` | the submitted SRAM and flash-wear audit turned into configuration-specific provenance, recovery, telemetry, SwapRAM, and durability gates |
| `ram-compression.md` | the compression options against RAM and flash pressure, with measured costs |
| `storage-techniques.md` | techniques from other small systems, and the SWAPRAM enable path |
| `flash-storage-expectation-plan.md` | the storage expectation audit verified against source: per-request Dhara checkpoint padding as the cost driver, the `fs_fmod` gap in `sync()`, and a phased plan with gates and predictions |
| `libc-size-audit.md` | where the C library's bytes go, across the shipped programs |
| `ufs-fixed-table-sram-reduction.md` | the SRAM the fixed UFS tables cost and how it shrinks |
| `constrained-c.md` | C techniques that hold inside a 144 KB process window |
| `c17-formatting-linting.md` | the staged C17 formatter, compiler-warning and semantic-linter setup, source classes, calibration fixtures and footprint-proof boundaries |
| `c17-semantic-migration-plan.md` | the C17 migration beyond syntax: dialect rule, claim-specific evidence, risk tiers, ARMv6-M alignment and Smaller C obligations, the row schema, six completion ledgers and six seed rows |
| `c17-shipped-build-baseline.md` | configuration-specific shipped source membership, artifact-bound static resource receipts and the acceptance states left open |
| `libc-passwd-header-inclusion.md` | repeated password-header inclusion, preserved public types/layout, captured callers, native compatibility and paired static resources |
| `shipped-archive-selection-comparison.md` | complete kernel, packed-program and native archive selection; schema-2 comparison, historical receipts and calibrated dependency-growth rejection |
| `syscall-dispatch-header-contract.md` | generated private syscall declarations, configuration-selected dispatch checks and native-header boundaries |
| `rp2040-usercopy-span-admission.md` | overflow-safe RP2040 `copyin` and `copyout` span admission, exact ILP32 behavior and direct-address residuals |
| `rp2040-syscall-stack-argument-admission.md` | complete RP2040 syscall stack-tail admission, stale-state clearing and dispatch failure behavior |
| `netbsd-11-micro-backports.md` | NetBSD 11 micro-fixes composed into C17 libc and cat contracts, target footprint evidence, and the remaining candidate frontier |
| `userland-bounded-io-memory.md` | bounded `tee` and `du`, strict `resize`, secret-memory primitives, BSD provenance, and complete RP2040 size accounting |
| `libc-opendir-directory-cloexec.md` | opened-descriptor directory admission, close-on-exec setup, calibrated cleanup gates, and measured stack, linked and packed-root costs |
| `libc-closedir-result-contract.md` | integer close results, errno preservation, exactly-once stream release, calibrated controls and paired resource evidence |
| `libc-getwd-entry-lifetime.md` | borrowed directory-entry lifetime, real-free sanitizer counterexample and calibrated traversal gates |
| `libc-ascii-casefold-contract.md` | exhaustive POSIX byte-locale mapping, signedness independence and measured table removal |
| `stdio-output-wrapper-contract.md` | formatted output counts, temporary stream ownership, write-failure controls and selected archive closure |
| `stdio-formatter-count-contract.md` | checked formatter arithmetic, logical counts, early stream failure and native consumer acceptance |
| `stdio-output-batch-capacity.md` | private 256-byte batching, frame and write-count tradeoffs, chunk failure controls |
| `board-capacity-acceptance.md` | board-only sampled-stack probe, calibration, exact image boundaries, the shared-break repair runs and open physical resource measurements; `board-capacity-acceptance-93b709fc.txt` holds the merged-source run transcript and `board-capacity-acceptance-passwd-edges.txt` the passwd edge-case steps |
| `kernel-uio-typed-interfaces.md` | four typed transfer definitions, caller preconditions and calibrated kernel inventory |
| `libc-scandir-allocation-contract.md` | selected-entry allocation, compact self-describing records, typed per-call sorting, calibrated failure gates and paired target resource evidence |
| `bsd-workspace-directory-stream-c17.md` | directory-stream ideas composed from six BSD trees into a 68-byte smaller C17 stream with allocation-free cookies and resident-buffer seeks |
| `STYLE-GUIDE.md` | the constrained-C and C17 migration proposal: resource accounts, C17isms, POSIX/BSD boundaries, quarantined historical sources, smaller-system evidence, and the proof a size claim owes |
| `211bsd-patch-scope.md` | bounded 2.11BSD textual candidates, historical comparison measurements, patch 499's measured stdio cost, and the measured `_doscan` migration unit |
| `211bsd-semantic-ledger.md` | selected per-fix dispositions, donor roles, pinned JSON ledger and execution receipts, resource consequences and explicit residuals |
| `211bsd-fwalk-report.md` | a defect in patch 499's `_fwalk`, drafted for the 2.11BSD maintainer and unsent |
| `211bsd-fwalk.patch` | the `_fwalk` fix for 2.11BSD patch 499 as a standalone `patch -p0` file with its message, the companion to the report |
| `stdio-torek-evaluation.md` | the patch-499 stream core built and measured on this target: a median +534 bytes per program and a breached packed-root budget, so declined, with two small mechanisms queued |
| `renode-login-regression.md` | the Renode login timeout was `panic: wakeup` behind a reboot prompt: three locores stored p_addr at a literal 60 after #129 moved it to 64; sys/proc_asm.h and a _Static_assert in kern_proc.c pin the offset, and boot.robot logs the UART transcript on failure |
| `stdio-unbuffered-ownership.md` | descriptor-independent input storage in the existing FILE byte, calibrated pushback and allocation-failure tests, and linked resource accounting |
| `stdio-core-c17-unit.md` | the V7 stdio core converted to C17 with a one-byte pushback slot in `FILE`: what changed, the calibrated gate, and the measured +32 data and +20 to +92 text per program |
| `stdio-ansi-surface-unit.md` | the C17 stdio interface outside the stream core: a shared mode-string reading with `O_APPEND` and the `x` modes, `fflush(NULL)`, `fgetpos` and `fsetpos`, the limit macros, the calibrated gate, and the measured three root blocks |

### Userland, tools and languages

| Document | Establishes |
| --- | --- |
| `posix-utilities.md` | the current selected-image utility ownership map and the superseded September 2026 porting survey |
| `sus-utility-ledger.json` | the machine-checked source, manifest, multicall, Bourne-builtin and behavior-gate state for 107 utility names |
| `sus-edition-contracts.md` | bounded Issue 7 and Issue 8 utility, directory, case-folding, process-group and runtime-limit contracts with explicit option marks and residuals |
| `sus-requirement-corpus.md` | the source-bound Issue 7/Issue 8 denominator, requirement-facet decomposition and RP2040 behavior-preserving translation rules |
| `sus-requirement-corpus.json` | machine-checked archive identities, entity counts, facet taxonomy and bounded tiny-system mappings |
| `sus-requirement-entities.tsv` | 1,617 copyright-safe Base Definitions, System Interfaces and utility entity keys with per-edition source membership |
| `dpucp-2024.md` | the non-conformance DPUCP-2024 contract, finite Issue 8 accounting, C17 language boundary, evidence ladder and per-surface size rule |
| `dpucp-2024-profile.json` | versioned DPUCP-2024 identity, denominator, language modes, hardware limits, state counts and witness catalog |
| `dpucp-2024-requirements.tsv` | 1,518 Issue 8 requirement rows with applicability, implementation, evidence, residual and next-action states |
| `dpucp-2024-donor-ledger.json` | pinned BSD donor mechanisms, licenses, invariants, dependency closures, C17 rewrites, promotion state, size evidence and deciding gates |
| `dpucp-2024-multicall-composition.md` | the seven-box shipped-name denominator, archive-member closure, 433-block standalone comparison and zero-block `logname` root delta |
| `dpucp-2024-multicall-composition.json` | machine-checked multicall dispatch, link-map, packed-image, root-accounting and source-identity evidence |
| `dpucp-2024-multicall-libc-members.tsv` | the 192-member libc archive closure for seven multicall boxes and the shell |
| `dpucp-2024-multicall-sizes.tsv` | retained per-program `hsaout` measurements for seven multicall images and 71 primary standalone applets |
| `dpucp-2024-multicall-base-receipt.json`, `dpucp-2024-multicall-candidate-receipt.json` | canonical clean-distribution, manifest and packed-root image receipts for the zero-block `logname` delta |
| `shell-c17-contracts.md` | the production Bourne shell's strict-C17 declaration boundary, corrected diagnostic census, host conditional coverage, ILP32 residual and linked-image measurements |
| `libc-array-memory-line-reading.md` | C17 `reallocarray`, `memmem`, `getdelim` and `getline` promotion, behavior gates, archive selection and complete RP2040 size accounting |
| `libc-array-memory-line-reading.json` | machine-checked Task 4 root-block arithmetic and bounded mechanism stack and line-buffer measurements |
| `libc-process-runtime-interfaces.md` | zero-argument `getpgrp`, bounded runtime selectors, stripped board-libc symbols and complete RP2040 size accounting |
| `posix-gap-tools.md` | five tools that close named POSIX gaps |
| `editors-constrained.md` | screen editors surveyed against the constraint |
| `vi-port.md` | modal vi/ex for this target |
| `ondevice-c-compilers.md` | native C compilers that can run on the board |
| `unifdef-off-manifest-measurement.md` | the bounded C17 unifdef port, its calibrated contracts, and its measured but unshipped RP2040 cost |
| `smlrc-rp2040-tuning.md` | tuning the native Smaller C Thumb-1 back end |
| `llama89-and-toolchain.md` | llama89.c and a native Thumb-1 toolchain |
| `ondevice-languages.md` | language runtimes beyond C |
| `femtollm.md` | what a language model fitting in 25 KB would be |
| `legacy/pdp11-v6/docs/research/v6-emulator-on-discobsd.md` | the isolated PDP-11 process emulator and Sixth Edition UNIX guest |

### Scope, alternatives and review

| Document | Establishes |
| --- | --- |
| `arm-main-legacy-build-isolation.md` | the canonical ARM tuples, default-off legacy options, finite relocation proof, shared-artifact boundary, calibrated gates, and restoration criteria |
| `retrobsd-port-scope.md` | the original scoping of a RetroBSD/DiscoBSD port to this chip |
| `os-options.md` | the Unix-like systems that target the Pico, compared |
| `dual-boot.md` | FUZIX and NuttX coexistence on one board |
| `bsd44-backport.md` | the 4.4BSD-Lite2 candidates worth backporting |
| `netbsd110-tinyspace.md` | NetBSD 1.0, the 1994 release and not NetBSD 10 or 11.0, compared to this port, and what still fits the 144 KB window as C17 |
| `integration-review.md` | sh-lineedit, stevie-vi and swapram reviewed together |
| `static-analysis.md` | the static analysis run over the port and what it found |
| `security-c17-frontier.md` | the shipped CVE dispositions, the bounded LZW repair and measured C17 migration, and the ranked security/resource frontier |
| `downloads-survey.md` | the downloads folder surveyed for usable material |
| `flash-id-build-routes.md` | the Pico SDK, CMake and the port's bmake measured against each other on the same probe |
| `warning-census.md` | what -Wall -Wextra costs the tree, as instances and as sites, where the warnings are, what the census records, and five ways of measuring it that lied |

### Audit ledgers and handbacks

Retained evidence. Each records the tree as it stood at the commit it
names; a correction goes in a new document rather than in the ledger.

| Document | Establishes |
| --- | --- |
| `warning-policy-audit.md` | the warning-enforcement gaps, the RP2040 extra-warning inventory, repair scope and remaining migrations |
| `root-audit-2026-09-15.md` | the root audit, with each claim ruled on |
| `manual-assessment-2026-09-16.md` | the manual assessed for a tester and for a buyer |
| `codex-findings.txt` | raw findings as the tool emitted them |
| `audit-handbacks/step5-libgcc-idiv-correction.md` | the AEABI signed-division provenance correction |
| `audit-handbacks/step5-nstatic-handback.md` | the NSTATIC resident-footprint audit |
| `audit-handbacks/step5-omagic-crosscheck-handback.md` | the OMAGIC candidate cross-check |
| `audit-handbacks/step5-printf-float-handback.md` | printf floating conversion, its source and linkage |
| `sixteen-bit-audit-response.md` | the 16-bit assumption audit ruled item by item: kernel items closed in `ce33939` with the `tsleep` bound gated since, the libtcl extent, cache, reference-count, separator, regexp and regsub defects, the `vmrate` counters and the `ps` UID width repaired with calibrated gates, retained narrow formats, and deferrals with their validation paths |
| `renode-rp2040-upstream-reports.md` | two issue texts for matgla/Renode_RP2040 with their `git am` patch series and the board measurement behind them, unsent |

### Accounts, licensing and the host side

| Document | Establishes |
| --- | --- |
| `security-profile.md` | the constrained account profile the image ships |
| `licensing.md` | what the image contains and what redistributing it requires |
| `legal-memo-redistribution.md` | redistribution as UF2 files and on pre-flashed boards, under US and California law |
| `host-macos-console.md` | the console from a Mac: drivers, Python, terminal, reflash |
| `host-windows-board-identity.md` | why Windows 11 did not find the board |
| `windows-board-round-two.md` | shell lists, typeahead, BOOTSEL and WinUSB on Windows 11 |
| `web-console-ux.md` | the web console as a first-time user meets it |
| `keen-unique-solution.md` | keen generated ambiguous puzzles; the fix and its evidence |
| `graft-context-graph.md` | graft as the context layer: what each surface answers, the appliance route, and where it misreads C |
| `graft-cache-access.md` | query-only shared cache access for Codex and Claude, local generation, freshness and rollback |
| `invariant-map.md` | six contracts (raw swap, packed exec, USB buffers, signal frames, heap growth, SwapRAM): what enforces each, what callers owe, what tests it, what stays open, with board results |
| `graft-concept-review.md` | the graft concept layer read against the six entries: what it retrieved, what it conflated, what it omitted |

## Other documentation in the tree

| Path | Covers |
| --- | --- |
| `README.md` | the port, the board, building, flashing, and first login |
| `NOTICE`, `LICENSE` | every component in an image and the terms that travel with it |
| `distrib/rp2040/README.md` | the distribution tree and the manifest |
| `distrib/rp2040/host/README.md` | the discobsd-host package for a person using it |
| `distrib/rp2040/host/DEVELOPMENT.md` | the per-OS toolchain table, packaging, and CI |
| `sys/arch/rp2040/README.md` | the port's source layout |
| `tools/analysis/README.md` | the analysis runners and where their handbacks land |
| `tests/rp2040/*/README.md` | one per board test, on what that test proves |
| `share/man/` | the manual pages the image ships |
