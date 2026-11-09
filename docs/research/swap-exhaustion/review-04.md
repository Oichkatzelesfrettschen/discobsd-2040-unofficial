# Review 04: kernel-resident SwapRAM pool and a 192-KiB user window

## Decision

The RP2040 port will use a permanent 192-KiB process window at
`0x20000000..0x20030000`. The Cortex-M0+ PMSAv6 MPU can cover the window with
one aligned 128-KiB region and one aligned 64-KiB region, so the larger window
does not consume another MPU region. The window leaves 59,392 bytes of ordinary
kernel RAM below `u0`. The three SwapRAM configurations retain at least 9,160
bytes of measured static-growth margin after their current kernel data,
`.ramfunc`, BSS, the 16-KiB pool, and the 4-KiB exchange bounce reserved by the
later in-place sector-exchange design.

The SwapRAM pool will remain a 16-KiB, eight-byte-aligned, `NOLOAD` linker
section, but the linker will place the pool after ordinary kernel data and BSS
instead of at the process-window boundary. The later exchange keeps its
4,096-byte reservation in the link budget; the present change does not allocate
or implement the bounce.

The LARGE epoch has no coherent meaning after the relocation. Its only resource
is the pool's former adjacency to the process window. Retaining
`SWAPRAM_BONUS`, `P_LARGE`, or the dynamic `user_top()` would hand a process
bytes beginning at `0x20030000`, which are ordinary kernel RAM in the selected
layout. The implementation must remove the epoch, its process flag, admission
path, sysctls, user-visible tests, and documentation. The independent
`machdep.swapram_evacuate` tier-drain operation remains useful and stays.

All three candidate sizes fit the measured images. The 160-KiB candidate would
make only the former 16-KiB LARGE bonus permanent. The 176-KiB candidate would
need a third user MPU region. The 192-KiB candidate provides the largest
proposed process limit while using the same two user MPU regions as 160 KiB.
The 9,160-byte worst-case static margin is narrower than the other candidates,
but the linker and headroom gates turn exhaustion of that margin into a build
failure rather than a runtime overlap.

## Artifact, field, and evidence boundary

The reviewed artifact is the RP2040 physical SRAM layout and its consumers in
the linker, kernel, user ABI, host gates, board programs, and documentation.
The relevant fields are systems memory ownership, ARMv6-M MPU configuration,
and reproducible cross-building. The review searched the complete tracked tree
and inspected all current source paths named below. Generated kernel products
were rebuilt from source before measurement.

The source revision is `71cc07fcc8b90c25e3cc0e3722eb0f1c48abd27e` on
`kern/user-window-relocation`. The resource survey cited by `brief-04.md` is
commit `bdd7d498` and was later merged as `3f271eee`; the feature branch's base
predates that documentation-only merge. `git show` supplied the retained
review, while the current worktree supplied every code and ELF measurement.

The host build used GCC 16.2.0, GNU Binutils 2.47.20260726, bmake 20260912, and
`PYTHON=/usr/bin/python3`. The locked command

```sh
export PYTHON=/usr/bin/python3
bmake MACHINE=rp2040 logged-kernel
```

completed with exit 0 and zero warnings in 38.383 seconds. Its immutable
invocation identifier is
`rp2040-kernel-local-20261109T045521Z-32-7124c3df`. The build executes host
tools and links all four configurations; it does not execute a kernel or touch
a board.

The section and symbol measurements used:

```sh
for configuration in PICO PICO_UART PICOCALC PICOCALC_PSRAM; do
    elf="sys/arch/rp2040/compile/$configuration/unix.elf"
    sha256sum "$elf"
    arm-none-eabi-readelf -SW "$elf"
    arm-none-eabi-nm -n -S --defined-only "$elf"
done
```

## Current linked SRAM measurements

`readelf -SW` supplies the allocated section sizes. The requested
`.data + .bss` column excludes `.swapram_pool`. The physical static column also
includes `.ramfunc` and linker alignment between `_sdata` and `_ebss`; that
column is the correct operand for the physical SRAM budget. PICO_UART carries
no SwapRAM pool in its configuration.

| Configuration | ELF SHA-256 | `.data` | `.bss` | `.data + .bss`, pool excluded | `.ramfunc` and alignment | Physical static bytes, pool excluded | Pool | `_ebss` | Unallocated bytes to `u0` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PICO | `9e94b762d4db68ef1c70296af60390b6db068659ac35c4e8c2fdd65a6bcf0083` | 256 | 15,272 | 15,528 | 320 | 15,848 | 16,384 | `0x2002bde8` | 76,312 |
| PICO_UART | `091bce609d3fc02998b6fcc4a5b112aefd50b639a0a038b7c79e6af52ec4cb12` | 200 | 14,832 | 15,032 | 312 | 15,344 | 0 | `0x20027bf0` | 93,200 |
| PICOCALC | `70a43f916c84f2a0a2c84da1d02cd16c859e199cd7fdb8a884decbd3e267262b` | 376 | 28,512 | 28,888 | 328 | 29,216 | 16,384 | `0x2002f220` | 62,944 |
| PICOCALC_PSRAM | `c33adcf0ad752e73c9742b0a1c0d95f9ecf92d139648aa27822ced55f38c5e50` | 424 | 28,664 | 29,088 | 664 | 29,752 | 16,384 | `0x2002f438` | 62,408 |

The current pool occupies `.swapram_pool` at
`0x20024000..0x20028000` in each SwapRAM configuration. The section has
eight-byte alignment. The shared heatshrink workspace is a separate
1,040-byte object in USB DPRAM; moving the pool does not move or resize that
workspace (`swapram.c:64-68,155-163`, `kern.ldscript:83-85,141-173`).

The current numbers differ by 8 bytes from two rounded figures copied into
`brief-04.md`: the current PICO gap is 76,312 bytes and the current
PICOCALC_PSRAM ordinary static footprint is 29,752 bytes. The source revision,
toolchain, ELF hashes, section table, and symbols above bind the replacement
measurements.

## Candidate budgets

`u0` remains fixed at `0x2003e800`. A candidate's raw kernel capacity is
therefore `0x2003e800 - window_end`. The table subtracts the actual pool for
each configuration, the reserved 4,096-byte bounce, and the physical static
bytes measured above. "After pool and bounce" is kernel capacity before the
ordinary static image; "growth margin" is the unallocated result after all
three charges.

| Window | Configuration | Raw kernel RAM | Pool | Bounce | After pool and bounce | Growth margin |
|---:|---|---:|---:|---:|---:|---:|
| 160 KiB | PICO | 92,160 | 16,384 | 4,096 | 71,680 | 55,832 |
| 160 KiB | PICO_UART | 92,160 | 0 | 4,096 | 88,064 | 72,720 |
| 160 KiB | PICOCALC | 92,160 | 16,384 | 4,096 | 71,680 | 42,464 |
| 160 KiB | PICOCALC_PSRAM | 92,160 | 16,384 | 4,096 | 71,680 | 41,928 |
| 176 KiB | PICO | 75,776 | 16,384 | 4,096 | 55,296 | 39,448 |
| 176 KiB | PICO_UART | 75,776 | 0 | 4,096 | 71,680 | 56,336 |
| 176 KiB | PICOCALC | 75,776 | 16,384 | 4,096 | 55,296 | 26,080 |
| 176 KiB | PICOCALC_PSRAM | 75,776 | 16,384 | 4,096 | 55,296 | 25,544 |
| 192 KiB | PICO | 59,392 | 16,384 | 4,096 | 38,912 | 23,064 |
| 192 KiB | PICO_UART | 59,392 | 0 | 4,096 | 55,296 | 39,952 |
| 192 KiB | PICOCALC | 59,392 | 16,384 | 4,096 | 38,912 | 9,696 |
| 192 KiB | PICOCALC_PSRAM | 59,392 | 16,384 | 4,096 | 38,912 | 9,160 |

The budget deliberately charges the later bounce even though this branch does
not allocate it. The eventual exchange implementation must add an explicit
linked object and reduce the reported margin by the object's actual size; the
present reservation prevents the layout choice from spending those bytes in
advance.

## MPU decomposition

RP2040 implements the ARMv6-M PMSAv6 region rules: each region size is a power
of two, and each base is aligned to that size (RP2040 datasheet table 117,
indexed by `sys/arch/rp2040/doc/DATASHEET-INDEX.md`). The 128-KiB low region at
`0x20000000` is common to every candidate.

| Window | Remaining decomposition | Bases | User regions | Verdict |
|---:|---|---|---:|---|
| 160 KiB | 32 KiB | `0x20020000` | 2 | Aligned and feasible. |
| 176 KiB | 32 KiB + 16 KiB | `0x20020000`, `0x20028000` | 3 | Aligned, but consumes a fifth total map entry for less space than the 192-KiB option. |
| 192 KiB | 64 KiB | `0x20020000` | 2 | Aligned, feasible, and selected. |

`mpu.c:111-126` currently pins the low region to log2 17, the high region to
log2 14, their sum to `USER_DATA_SIZE`, and only the low base's alignment. The
implementation must set the high region to log2 16 and add a static assertion
for its own base alignment. `usr.bin/mputest` continues to expect four total
programmed regions because the selected decomposition does not change the
region count.

## Window and pool consumer inventory

### Authoritative definitions and kernel behavior

| Consumer | Current dependency | Required disposition |
|---|---|---|
| `sys/arch/rp2040/include/machparam.h:118-131` | Defines the 144-KiB size, end, `MAXMEM`, and user stack macros. | Set 192 KiB; retain derived end and stack macros. |
| `sys/arch/rp2040/conf/RP2040.ld:37-62` | Defines `USERRAM`, starts `RAM` at `0x20024000`, and fixes `u0`. | Set `USERRAM` to 192K and `RAM` to `0x20030000..0x2003e800`. |
| `sys/arch/rp2040/conf/kern.ldscript:15-21,76-120,175-195` | Publishes bounds, places the pool first in RAM, and asserts adjacency and complete tiling. | Place the pool after BSS, retain `NOLOAD` and alignment, remove pool/window adjacency, and assert pool containment below `u0`. |
| `sys/arch/rp2040/rp2040/mpu.c:111-139` | Tiles the window with 128-KiB and 16-KiB regions. | Use 128-KiB and 64-KiB regions and assert both alignments. |
| `sys/arch/rp2040/rp2040/machdep.c:586-600` | Checks linker size and calls the adjacency-dependent `swapram_init()`. | Keep the size check; remove the obsolete pool/window boot check. |
| `sys/sys/systm.h:90-103` | Makes `USER_TOP` process-dependent and grants `USER_WINDOW_MAX` a pool bonus. | Make both fixed at the permanent window. |
| `sys/kern/exec_subr.c:435-467` | Selects the epoch, migrates the argument spool, and lays the stack below dynamic `USER_TOP`. | Admit against fixed `MAXMEM`; retain stack layout under fixed `USER_TOP`. |
| `sys/kern/exec_aout.c:141,197`, `sys/kern/exec_elf.c:160,192`, `sys/arch/rp2040/rp2040/exec_hsaout.c:128,179` | Admit headers through `USER_WINDOW_MAX` and place stacks through `USER_TOP`. | Rebuild against the fixed macros. |
| `sys/kern/kern_mman.c:27-49` | Bounds `brk()` with `swapram_ceiling()` and the live stack base. | Bound with fixed `MAXMEM`; retain the stack-collision check. |
| `sys/kern/vm_swap.c:63`, `sys/kern/kern_sig.c:599` | Derive swap-in and core stack addresses from `USER_TOP`. | Rebuild against fixed `USER_TOP`. |
| `sys/arch/rp2040/rp2040/{fault.c,sig_machdep.c,syscall.c,machdep.c}` | Derive fault, signal, syscall, and user-copy bounds from `USER_TOP`. | Rebuild against fixed `USER_TOP`. |
| `sys/kern/init_main.c:196`, `sys/arch/rp2040/rp2040/locore0.S:184` | Derive process-0 stack placement from `__user_data_end`. | No source edit; relink supplies the new end. |
| `lib/libc/arm/gen/rom_float_resolver.S:47` | Uses `USER_DATA_END` for descriptor admission. | Clean rebuild supplies the new end. |
| `bin/ps/ps.c`, `usr.bin/w/w.c` | Translate stack addresses with the machine stack macros. | Clean rebuild supplies the new end. |
| `usr.bin/mputest/mputest.c:71,237,377,443-466` | Expects four map entries and derives boundary probes from `USER_DATA_END`. | Retain four entries; clean rebuild moves the probes. |

### Active host and model gates

| Gate or fixture | Old assumption | Required disposition |
|---|---|---|
| `tools/renode/check-boot.py:60,71` | Exact 144-KiB MPU and user-memory banners. | Expect 192 KiB and the 64-KiB high region. |
| `tools/c17/shipped_resources.py:16-18,235` and its tests | Current process limit is 147,456 bytes and kernel RAM starts at `0x20024000`. | Set 196,608 bytes and `0x20030000`; update calibrated fixtures. |
| `tests/qemu_aout` runner, probe, expected output, and Makefile | Maps and probes a 144-KiB process window. | Map and probe 192 KiB; preserve one-past-end refusal. |
| `tests/aout_header/aout_header_test.c` | Header checker caps the image at 144 KiB. | Cap at 192 KiB. |
| `tests/kernel/exec_elf_*admission*_test.c` | ELF fixtures cap the image at 144 KiB. | Cap at 192 KiB. |
| `tests/rp2040/usercopy_span/check.py` | Checks the 144-KiB full span and final byte. | Check the 192-KiB span and final byte. |
| `tests/kernel/usercopy_span_test.c` | Models a conditional 144-to-160-KiB extension. | Model one fixed 192-KiB window and remove epoch-only cases. |
| `tests/kernel/rwuio_test.c` | Uses the old end and a conditional 160-KiB extension. | Use the fixed 192-KiB end and remove epoch-only extension cases. |
| `sys/arch/rp2040/test/swapram` shims and fixture | Defines 144-KiB `MAXMEM` and exercises dynamic ceiling/top. | Set 192 KiB and remove epoch-only assertions while retaining evacuation. |
| `tools/verify_rp2040_swapram_link.sh` | Verifies pool size and the independent DPRAM codec workspace. | Add pool alignment and kernel-RAM containment checks without pinning the old address. |

`tools/check_kernel_headroom.py:217` uses `0x20024000` only as a synthetic
non-flash `PT_LOAD` address; that value does not model the live RAM boundary
and needs no semantic change. The DPUCP donor-ledger and board-acceptance
validators intentionally pin retained 147,456-byte historical records. Those
values describe the evidence revision that produced each record and must stay
unchanged.

### Current documentation and retained evidence

The active layout contract appears in `AGENTS.md` and its generated
`.claude/CLAUDE.md` copy, the root and RP2040 READMEs, `BOOT-MAP.md`, `MPU.md`,
`STORAGE.md`, `TESTING.md`, `CAPACITY.md`, and `PICOCALC.md`. Current research
contracts include `docs/research/invariant-map.md`,
`rp2040-usercopy-span-admission.md`, `rp2040-signal-context-admission.md`,
`rp2040-memory-wear-engineering-program.md`, and
`sys/arch/rp2040/doc/research/utilbox.md`. The implementation must update their
current claims or add an explicit superseding reference.

The swap-exhaustion audit, briefs, `review-02.md`, `review-03.md`, the
memory-ownership evidence, XIP shared-libc findings, board inventory, capacity
corpora, DPUCP JSON records, and PDP-11 research preserve measurements or plans
from older revisions. Their old values remain evidence facts. The new review
must be cited from any retained note whose conclusion would otherwise look
current; the captured measurements and structured record values must not be
rewritten.

Several program comments use 144 KiB as a current resource argument rather
than an executable constant: `bin/tar/tar.c`, `lib/libc/gen/malloc.c`,
`lib/libc/stdio/doscan.c`, `games/pzcommon/pzfront.c`, and
`usr.bin/as/README.md`. The documentation commit must revise those live
capacity explanations. Text that explicitly records an older measured build
keeps its value and gains a superseding citation where necessary.

## LARGE epoch removal inventory

The epoch transition has one resource invariant: the pool is empty, closed,
and lent to a `P_LARGE` process. `swapram_enter_large()` posts a request,
`swapram_service()` evacuates the pool, `user_top()` adds the pool size, fork
inherits the flag, and exit or a smaller exec releases it. Pool relocation
destroys the lending operation. Leaving only the flag or sysctl would create a
false interface; leaving the ceiling arithmetic would create a kernel-memory
write primitive.

| Removed interface or mechanism | Callers and readers that must leave with it |
|---|---|
| `SWAPRAM_SMALL`, `SWAPRAM_LARGE`, `SWAPRAM_BONUS` | `swapram.c`, `sys/systm.h`, exec admission, sysctl dispatch, and host epoch fixtures. |
| `P_LARGE` | `kern_fork.c` inheritance, `kern_exit.c` release, exec admission, `kern_mman.c`, `swapram.c`, and the host shim. |
| `swapram_epoch`, `sr_nlarge`, `sr_large_want`, `sr_small_want`, `sr_large_seq` | `swapram_service()`, epoch sleepers/wakeups, sysctl readers/writers, and host state assertions. |
| `swapram_nlarge()`, `swapram_ceiling()`, dynamic `user_top()` | sysctl, `brk()`, exec admission, `USER_TOP`, and swap/fault/signal/user-copy consumers that will use the fixed macro. |
| `swapram_enter_large()`, `swapram_leave_large()`, `swapram_inherit()`, `swapram_set_epoch()` | exec, fork, exit, and `machdep.swapram_epoch`. |
| `swapram_init()` pool-at-window-end check | `machdep.c` boot initialization and the linker adjacency assertion. |
| `exec_spool_to_flash()` | Its sole production caller in LARGE exec admission and its direct host migration test. Ordinary initial flash fallback remains. |
| `machdep.swapram_epoch` and `machdep.swapram_large` | `CPU_SWAPRAM_EPOCH`, `CPU_SWAPRAM_LARGE`, generic `sysctl(8)` names, `epochtest`, and `bigtest`. Numeric machdep slots 10 and 11 remain reserved so `CPU_MPU` and later identifiers keep their ABI numbers. |
| `tests/rp2040/swapram_epoch` and `tests/rp2040/swapram_large` | Their parent Makefile entries, capacity-corpus cases, dependency edges, calibration selection, C17 source paths, and TESTING descriptions. |
| Dynamic-ceiling host coverage | Epoch cases in `swapram_evac_test.c`, `usercopy_span_test.c`, `rwuio_test.c`, and `exec_spool_test.c`. Evacuation, fixed-boundary, and ordinary spool coverage remain. |

The independent `machdep.swapram_evacuate` request, `swapram_evacuate()`,
`swapram_images()`, transactional flash reservations, host evacuation tests,
board evacuation tool, and capacity-corpus evacuation case do not depend on
pool adjacency. They remain active. `swapram_service()` becomes the service for
that request alone and reopens admission after either evacuation result.

## Adversarial findings and falsifiers

### FATAL -- a retained bonus would expose kernel RAM to a process

Grounding: the selected window ends at `0x20030000`, exactly where linker
`RAM` begins. The current `user_top()` and `USER_WINDOW_MAX` add 16 KiB for a
`P_LARGE` process. A retained epoch would therefore authorize
`0x20030000..0x20034000`, which contains `.data`, `.ramfunc`, BSS, or the
relocated pool. Remedy: remove the epoch and every dynamic-ceiling interface.

### SERIOUS -- moving only the linker boundary would leave independent oracles stale

Grounding: the host gates listed above embed 144 KiB or `0x20024000` in
independent admission, QEMU, user-copy, resource, and Renode models. A kernel
that links while those tests keep the old values has not established the new
contract. Remedy: update every active oracle in the same window commit and run
the full non-hardware suite.

### SERIOUS -- pool relocation needs a positive containment invariant

Grounding: the current link assertion proves adjacency, not merely non-overlap.
Deleting it without a replacement would let an alignment or section-order edit
move the pool outside ordinary kernel RAM or across `u0`. Remedy: assert
eight-byte alignment, exact section ownership, and a pool end at or below
`ORIGIN(RAM) + LENGTH(RAM)`; extend the linked-object verifier.

### SOUND -- every candidate fits current static RAM

Grounding: the candidate table charges the current physical static footprint,
configured pool, and reserved bounce. Every row remains positive. The smallest
margin is 9,160 bytes for PICOCALC_PSRAM at 192 KiB. The conclusion is bounded
to these four linked ELFs and this toolchain; it does not establish runtime
stack, buffer, or device workload headroom.

### SOUND -- the selected MPU tiling obeys PMSAv6 alignment

Grounding: `0x20000000` is aligned to 128 KiB and `0x20020000` is aligned to
64 KiB. The regions meet at `0x20020000` and end at `0x20030000`. Remedy for
future drift: retain the size-sum assertion and add an assertion for the high
base alignment.

The following observations falsify the selected design: any linked
configuration consumes more than its row's growth margin; the pool loses
eight-byte alignment or leaves kernel RAM; the 128-KiB and 64-KiB MPU regions
fail programmed readback; a current program, gate, or document still treats
144 KiB, `0x20024000`, or LARGE as the live contract; or the full build, check,
and kernel-headroom gates report a warning or nonzero exit.

## Steelman and verdict

The old LARGE epoch was internally coherent only while the pool immediately
followed the 144-KiB process window: evacuation transferred ownership of the
same physical bytes from compressed images to one larger resident image. The
mechanism had real host coverage and explicit ownership transitions. Pool
relocation removes the physical bytes that made those transitions meaningful,
so preserving the state machine would preserve ceremony rather than a resource.

The permanent 192-KiB layout is sound for the four measured configurations.
The decisive properties are the 9,160-byte positive worst-case static margin,
the two-region aligned MPU tiling, and complete removal of the dynamic bonus.
Trust requires the implementation to replace the adjacency assertion with pool
containment, update every active independent oracle, keep historical evidence
values historical, and pass all acceptance gates without warnings.
