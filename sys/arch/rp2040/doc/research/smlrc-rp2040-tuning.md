# Tuning the native Smaller C Thumb-1 back end for the RP2040

`usr.bin/smlrc/cgthumb.c` compiles user programs on the board itself, and the
programs it writes execute from the 96 KB SRAM user window, so a generated
byte costs both residency and the cycles that fetch it. This records what the
back end emits today against the Cortex-M0+ instruction set and timing, what
changed, and what was measured and declined.

Authority for every architectural claim below is a file in the Pico SDK at
`/usr/share/pico-sdk` or in this tree; no RP2040 datasheet PDF is present on
this host, so SDK register headers stand in for the datasheet sections they
transcribe. Cycle counts come from the Cortex-M0+ timing the tree already
uses elsewhere: data processing one cycle, taken branch three, load or store
two, `MUL` one on the RP2040's fast multiplier.

## Baseline

Host toolchain `arm-none-eabi-gcc` with `qemu-arm`, corpus
`usr.bin/smlrc/tests/t[0-9][0-9]_*.c`, `.text` of each assembled object as
reported by `arm-none-eabi-size -A`. `t17_align.gnu` is the cross compiler's
companion object and is excluded from the totals because smlrc does not write
it.

| program | baseline | after ADD SP | after imm3 | delta |
| --- | ---: | ---: | ---: | ---: |
| t01_arith | 872 | 868 | 804 | -68 |
| t02_ptr | 584 | 580 | 544 | -40 |
| t03_struct | 528 | 520 | 500 | -28 |
| t04_string | 532 | 528 | 528 | -4 |
| t05_switch | 416 | 408 | 376 | -40 |
| t06_recur | 604 | 584 | 584 | -20 |
| t07_global | 416 | 408 | 396 | -20 |
| t08_list | 492 | 488 | 472 | -20 |
| t09_printf | 412 | 408 | 408 | -4 |
| t10_loops | 992 | 984 | 920 | -72 |
| t11_types | 500 | 496 | 492 | -8 |
| t12_echo | 796 | 796 | 780 | -16 |
| t13_cat | 3996 | 3984 | 3932 | -64 |
| t14_stress | 14412 | 14380 | 12608 | -1804 |
| t15_divreg | 1532 | 1528 | 1492 | -40 |
| t16_float | 1184 | 1180 | 1148 | -36 |
| t17_align | 2544 | 2528 | 2528 | -16 |
| **total** | **30812** | **30668** | **28512** | **-2300 (-7.5%)** |

`usr.bin/as/tests/thumb-native.s` is generated from `t01_arith.c` and tracks
that row: 872 bytes of text at the baseline, 804 after both changes, and the
tree's own assembler agrees with `arm-none-eabi-as` on every unrelocated
halfword of the regenerated file.

The compiler's own residency, measured on the rp2040 build at
`distrib/obj/destdir.rp2040/usr/libexec/smlrc` with `tools/bin/size`:

| stage | text | data | bss |
| --- | ---: | ---: | ---: |
| baseline | 47896 | 1372 | 26328 |
| after ADD SP | 47780 | 1372 | 26328 |
| after imm3 | 47856 | 1372 | 26328 |

The back end ends 40 bytes smaller than it started.

Gate state at the baseline, unchanged by either commit: `bmake -C usr.bin/smlrc
test` reports `passed 16, failed 1, link-only 0`, the single failure being
`t16_float`, which reads the Boot ROM function table at address 0x10 and
segmentation-faults under `qemu-arm` because that address is unmapped there.
`spalign.py` reports `311 calls, all at SP mod 8 == 0; 10 back-end-private
helper calls exempt`. Instruction counting under `qemu-arm` is **not run**:
the emulator's `-d in_asm` records translated blocks rather than executions
and no counting plugin is installed on this host, so every cycle figure below
is a static count against the Cortex-M0+ table rather than a trace.

## Landed: reserve the frame with ADD SP by a negated constant

ARMv6-M encodes `ADD (SP plus register)` and offers no `SUB (SP plus
register)`, which is why `GenFxnProlog` materialized the frame size and wrote
SP back through `mov`, `subs`, `mov`. The frame size is unknown until the body
has been parsed, so it reaches the prolog through the `.LFn` symbol the epilog
defines; defining that symbol as the negated size makes the reserve a single
`add sp, rN`. `GenAddSp`'s out-of-range decrease loses the same triple.

Per function the prolog falls from 16 instruction bytes to 12, and two
instructions and two cycles leave every call. Corpus text falls 30812 to
30668. Both assemblers encode `add sp, r3` as `0x449d` and accept a negative
`.equ`, checked directly before the change; `spalign.py` was taught the
negative-constant SP form in the same commit, so its rule that an unmodeled SP
move is an error rather than a skip still holds.

## Landed: address a near frame slot with the 3-bit immediate ADDS/SUBS

Thumb-1 load and store offsets are unsigned and a local's offset from the
frame pointer is negative, so `GenLocalAddr` computes an address for every
local. It materialized the displacement into a register first, spending four
bytes and two cycles even for the displacement four. `ADDS` and `SUBS`
(register plus 3-bit immediate) name destination and source separately, so a
slot within seven bytes of the frame pointer is one halfword and one cycle.

Measured over the corpus before the change, 1040 of the frame-address
computations used displacement four -- the first local -- and the change
converts 1074 of 1414 such computations in total. Corpus text falls 30668 to
28512, and `t14_stress`, which is dense in locals, falls 14380 to 12608, or
12.3%. 340 computations remain at displacements of eight and above, where
`mov rD, r7` plus `subs rD, #imm8` is the same four bytes as the sequence it
would replace and nothing is gained.

## Declined, with the number that declined it

**Integer division through the SIO hardware divider.** `lib/libc/arm/gen/
aeabi_div.S` divides with a shift-and-subtract loop, and `ThumbDivMod` calls
it; `audit-findings.md` measures the linked integer-division helpers at 304
bytes. Simulating the two loops against six operand pairs gives 52 to 350
instructions and 66 to 472 cycles, a mean of 197, against the divider's eight.
The saving is real and it is declined because no sound userland validator
exists. `src/rp2040/hardware_regs/include/hardware/regs/sio.h:425-433` states
that `DIV_CSR.DIRTY` "changes to 1 when any register is written, and back to 0
when QUOTIENT is read", so after the helper's own `QUOTIENT` read the flag is
always clear and cannot witness a foreign writer. An operand readback -- re-read
`DIV_UDIVIDEND` and `DIV_UDIVISOR` after the result and retry on a mismatch --
fails under two preemptions: a first hands the divider to a process that
divides different operands, and a second writer that coincidentally restores
the original operands before the readback lets the wrong quotient pass. The
SDK's own answer is `hw_divider_save_state` and `hw_divider_restore_state`
(`src/rp2_common/hardware_divider/include/hardware/divider.h:495-515`),
executed by the preempting context, which is a kernel change this work is
scoped out of. `sys/arch/rp2040/doc/research/float-libs.md` section 4.1 admits
direct ROM division under the invariant that the kernel, IRQ, NMI and callout
graph contains no SIO-divider consumer; integer division in libc cannot
inherit that invariant, because its hazard is process-switch preemption
between the operand writes and the quotient read, which that invariant does
not cover. Software division stays.

**Multiplication.** `ThumbBinOpReg` emits `muls` directly and calls no helper,
which is correct on a core whose `MUL` is one cycle. The front end already
strength-reduces a power-of-two multiply: a probe compiling `x * 4` yields
`lsls r0, r0, #2`, `x * 1` yields nothing at all, and array indexing scales
with `lsls`. No over-eager reduction exists to undo -- `x * 3` stays `movs
r4, #3` plus `muls`, four bytes and two cycles, which beats a shift-and-add
pair on this core. `GenExpr0` excludes `*` from the constant path at the
dispatch, and routing it through `ThumbBinOpConst` instead produces the
identical `movs` plus `muls`, verified on a probe with a spilled left operand,
so the exclusion costs nothing. No change.

**64-bit shifts and comparisons.** Smaller C has no 64-bit integer type:
`tokLongLong` is commented out at `usr.bin/smlrc/smlrc.c:328`, `long` is the
32-bit word, and `cgthumb.c` emits no `__aeabi_l*` helper. The SDK's
`pico_int64_ops` sequences have nothing to be shorter than. Not applicable.

**Literal pools and collapsible branches.** `tools/analysis/thumb_peepholes.py`
over the linked `t14_stress` reports 36 long-conditional pairs of which 1 is
collapsible, and that one is in libc's `_flsbuf`, which GCC wrote: zero of the
conditional pairs `GenCondJump` emits are within the Thumb-1 conditional
branch range of +-256 bytes. The analyzer reports 0 stack reload/add/pop
triples, and the audit's finding that all 1304 PC-relative literal addresses
fail the same-section `ADR` test stands unchallenged on this corpus. What the
analyzer does find is 510 in-range `BL` instructions: `GenJumpUncond` spends
four bytes on every unconditional jump because a `BL` is the only branch
always in range, and the corpus holds 719 such sites. Collapsing the reachable
ones to the two-byte `B` would recover roughly 1 KB, and it needs a branch
relaxation pass. `cgthumb.c` is single-pass and emits the prolog before the
body is parsed -- the reason the frame size travels through a symbol at all --
so displacement is unknown where the branch is written, and `ThumbSpend`
conflates instruction bytes with pool bytes and cannot substitute for one.
That is the blocker, and it is an absent pass rather than an impossibility.

**Inline memory loops.** The back end inlines no memory operation. A structure
assignment calls one shared out-of-line helper that `GenFin` writes once per
translation unit, eight instructions copying backwards so the counter doubles
as the index; `memcpy`, `memset` and `strlen` are always calls into libc,
whose `memcpy` and `memset` members (`distrib/rp2040/boardlibc-members` lines
53-56) are C and reach no ROM routine. Routing those members through the Boot
ROM's `pico_mem_ops` entries is a libc change with its own measurement, not a
code generator change. No change here.

**RP2040 errata.** `grep -rhno 'RP2040-E[0-9]*' /usr/share/pico-sdk/src` over
the whole SDK source tree yields exactly three distinct numbers: E1 in
`hardware_regs/include/hardware/regs/watchdog.h`, E10 in
`hardware_rosc/include/hardware/rosc.h`, and E13 in
`hardware_dma/include/hardware/dma.h`. All three are peripheral errata --
watchdog tick, ring oscillator, DMA -- and none constrains an instruction the
compiler may emit. Nothing to work around.

## Gates

Both commits were gated identically and both passed at the baseline counts:
`bmake -C usr.bin/smlrc test` at `passed 16, failed 1` with `t16_float` the
known ROM-at-0x10 failure under `qemu-arm`; `usr.bin/smlrc/tests/fuzz.sh 60`
at `differential: 60/60 seeds match the host oracle`; `spalign.py` at `311
calls, all at SP mod 8 == 0`; `usr.bin/as/tests/thumb-encoding.sh` at
`encoding: 7 inputs agree with arm-none-eabi-as on every unrelocated
halfword` with `thumb-native.s` regenerated for each generator change; and
`bmake MACHINE=rp2040 build` exiting 0 with the two pre-existing warnings
(`kern/exec_subr.c:90` unused variable, and the linker's RWX segment note on
`unix`) and no new one.
