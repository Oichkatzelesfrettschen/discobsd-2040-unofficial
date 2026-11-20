# Proof that LTO-linked code stays off flash while XIP is suspended

Status: open decision, tracked by issue 765. The acceptance below is
proposed; no LTO kernel is admitted.

## Hazard

A flash erase, program or command takes execute-in-place (XIP) down for
both cores. An instruction fetch or literal load from the XIP window in
that interval returns undefined data. Link-time optimization (LTO) moves
code relative to its source and symbol names: it inlines across
translation units, creates split and specialized clones, and emits
library calls the source does not write.

LTO trials show three such movements. The trial recorded in
`picocalc-kernel-flash-budget.md` (compiler experiments) placed an
interrupt helper and a capacity-counter clone outside the hot span. A
bare-Pico kernel LTO link, whose log the tree does not retain, failed on
an emitted `memcpy` call and on the dropped assembly-only symbols
`arm_fault` and `fault_regs`; reproducing that link is part of the work.
On Cortex-M0+, GCC dispatches a `switch` through libgcc's
`__gnu_thumb1_case_*` helpers and divides through `__aeabi_uidiv`; both
live in flash unless placed otherwise.

## Current coverage

| Mechanism | What it establishes | Gap under LTO |
|---|---|---|
| `__ramfunc`: `noinline` and `section(".ramfunc")`, `sys/arch/rp2040/dev/flash.h` | the attributed function is linked into SRAM | callees, clones and emitted helper calls carry no attribute |
| `.ramfunc` load-end assertion, `tests/rp2040_load_span` | the SRAM initializer fits `FLASH` | placement of called code |
| `watchdog_site()` pairing, part of `check-core1-image` | every `exit_xip()` call sits inside a declared section | source text only |
| `tools/verify_rp2040_core1_image.py` | core 1's code reached from three roots lies in SRAM, by address in the final image | core 0's flash-off routines are not walked |

The PicoCalc kernels link no `core1.c`. Core 1 verification applies to
them once their configuration carries the `CORE1` option.

## Proposed acceptance

Three separate evidence classes, all on the same LTO image:

1. Final-image verifier. Extend the address walk of
   `verify_rp2040_core1_image.py` to every `.ramfunc` entry symbol in
   addition to core 1's roots. Each reached instruction, direct branch and
   call target lies in SRAM or the boot ROM; no literal word lies in the XIP
   window; indirect calls go only through the SRAM copy of the ROM table
   (`flrom` in `flash.c`) or core 1's ring handler. Because the walk
   follows addresses in the linked code, inlined bodies and clones are
   checked as instructions without depending on names. Calibration: the
   current `-Os` kernel passes; LTO links with `noinline` removed from a
   helper, an added `switch`, an added division and an emitted `memcpy`
   each fail with the defect named.
2. Emulation. `check-core1-emulation` already maps no flash for the
   core 1 instance. The core 0 instance unmaps the XIP window between its
   `exit_xip` and `flash_enter_xip` calls and drives erase, program and
   unique-ID reads; any flash fetch fails the run.
3. Board. Dhara writes under load on the LTO image, with core 1 running
   on a PICO kernel; `machdep.core1_stalls` stays unchanged and no fault
   is recorded.

Until all three pass, emulator results establish boot behavior for the
tested images only, and the production kernels keep their non-LTO build.

## Falsifier

The proposal fails when an LTO image that passes the verifier and the
emulation faults or corrupts a flash write on the board, which would
show a flash dependency the address walk cannot see, such as a data
access computed at run time.
