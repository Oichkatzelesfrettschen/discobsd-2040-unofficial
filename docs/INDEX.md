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
| How do I work on this tree? | `AGENTS.md` (`CLAUDE.md` links to it) |
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
| `libc-size-audit.md` | where the C library's bytes go, across the shipped programs |
| `ufs-fixed-table-sram-reduction.md` | the SRAM the fixed UFS tables cost and how it shrinks |
| `constrained-c.md` | C techniques that hold inside a 144 KB process window |
| `netbsd-11-micro-backports.md` | NetBSD 11 micro-fixes composed into C17 libc and cat contracts, target footprint evidence, and the remaining candidate frontier |
| `userland-bounded-io-memory.md` | bounded `tee` and `du`, strict `resize`, secret-memory primitives, BSD provenance, and complete RP2040 size accounting |
| `bsd-workspace-directory-stream-c17.md` | directory-stream ideas composed from six BSD trees into a 68-byte smaller C17 stream with allocation-free cookies and resident-buffer seeks |
| `STYLE-GUIDE.md` | the constrained-C and C17 style proposal: resource accounts, the four language profiles, the migration unit, and the evidence a size claim owes |
| `211bsd-patch-scope.md` | the fork point from 2.11BSD, what each patch since then touches here, what patch 499's stdio would cost this target, and the measured `_doscan` migration unit |
| `211bsd-fwalk-report.md` | a defect in patch 499's `_fwalk`, drafted for the 2.11BSD maintainer and unsent |
| `211bsd-fwalk.patch` | the `_fwalk` fix for 2.11BSD patch 499 as a standalone `patch -p0` file with its message, the companion to the report |
| `stdio-torek-evaluation.md` | the patch-499 stream core built and measured on this target: a median +534 bytes per program and a breached packed-root budget, so declined, with two small mechanisms queued |
| `renode-login-regression.md` | the Renode login timeout was `panic: wakeup` behind a reboot prompt: three locores stored p_addr at a literal 60 after #129 moved it to 64; sys/proc_asm.h and a _Static_assert in kern_proc.c pin the offset, and boot.robot logs the UART transcript on failure |
| `stdio-core-c17-unit.md` | the V7 stdio core converted to C17 with a one-byte pushback slot in `FILE`: what changed, the calibrated gate, and the measured +32 data and +20 to +92 text per program |
| `stdio-ansi-surface-unit.md` | the C17 stdio interface outside the stream core: a shared mode-string reading with `O_APPEND` and the `x` modes, `fflush(NULL)`, `fgetpos` and `fsetpos`, the limit macros, the calibrated gate, and the measured three root blocks |

### Userland, tools and languages

| Document | Establishes |
| --- | --- |
| `posix-utilities.md` | POSIX utility coverage and the porting candidates |
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
