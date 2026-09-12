# llama89.c and a native Thumb-1 toolchain for DiscoBSD on RP2040

Scope: the target is a Raspberry Pi Pico running DiscoBSD (2.11BSD-derived) on the RP2040
(dual Cortex-M0+, ARMv6-M, Thumb-1 only, no FPU, no hardware divide, 125 MHz), 264 KB SRAM
with a 96 KB per-process ceiling for text+data+bss+stack, a.out executables loaded whole into
RAM, and a 2 MB QSPI flash of which the root filesystem holds about 1 MB with about 120 KB
free. Flash is XIP-readable at bus speed but reached through a Dhara flash-translation layer,
so a user process cannot mmap it -- it reads files with `read(2)`.

## Part 1: what "llama89.c" is

A GitHub code-search and web search for the literal string `llama89.c` / `llama89` returns
zero repositories and zero relevant pages. No project by that name exists as of this writing
(searched September 2026). The name is almost certainly a conflation of two real, unrelated
things:

- **llama2.c**, Andrej Karpathy's ~700-line single-file inference engine for Llama-2-architecture
  models, MIT licensed ([karpathy/llama2.c](https://github.com/karpathy/llama2.c)). Its
  `README.md` states the license as "MIT."
- **llama98.c**, a fork of llama2.c retargeted at Windows 98 on 25-year-old x86 hardware
  ([exo-explore/llama98.c](https://github.com/exo-explore/llama98.c), mirrored at
  [a43501/llama98.c](https://github.com/a43501/llama98.c)). The "98" names a Windows version,
  not a C standard.
- Two genuine C89 ports of llama2.c exist and would be the closest match to "a C89 port of
  llama2.c": [yl01inve/llama2.c_dos](https://github.com/yl01inve/llama2.c_dos), which documents
  building with `gcc -std=c89 run.c -o run.exe`, and
  [yeokm1/dosllam2](https://github.com/dosllam2), a DOS port. Neither is named `llama89.c`.

Conclusion: treat "llama89.c" as shorthand for "a C89-clean build of llama2.c" rather than a
real, separately named artifact. The rest of this section evaluates upstream llama2.c (MIT
license) plus its `runq.c` int8-quantized variant against the RP2040's constraints, since that
is the actual object in question.

### Dependencies and memory model of upstream llama2.c

Fetching `run.c` directly from
[karpathy/llama2.c](https://raw.githubusercontent.com/karpathy/llama2.c/master/run.c) shows:

- **libm**: `sqrtf` (rmsnorm and attention-score scaling), `expf` (softmax, SwiGLU), `powf`
  and `cosf`/`sinf` (RoPE). All four must exist as software-float routines on a Cortex-M0+
  build (2.11BSD libc, softfp).
- **Weight loading is `mmap`-only, unconditionally.** `fopen`/`fread` is used only to read the
  small `Config` header; the checkpoint's weight tensors are then mapped with
  `mmap(NULL, *file_size, PROT_READ, MAP_PRIVATE, *fd, 0)`. There is no `USE_MMAP`-style
  `#ifdef` fallback to `fread` for the weights. Given the stated constraint that this device's
  flash is reached through a Dhara FTL and files are read with `read(2)`, not mapped, upstream
  `run.c` as written **does not run unmodified** on this device -- the mmap call has nothing to
  map onto.
- **Activation state (`RunState`) is allocated with `calloc`**, one buffer per tensor:
  `x`, `xb`, `xb2`, `q` sized `dim` floats each; `hb`, `hb2` sized `hidden_dim` floats each;
  `key_cache` and `value_cache` each sized `n_layers * seq_len * kv_dim` floats, where
  `kv_dim = dim * n_kv_heads / n_heads`; `att` sized `n_heads * seq_len` floats; `logits` sized
  `vocab_size` floats. The KV cache is the dominant term for any model with a non-trivial
  `seq_len`.

### The stories260K model, concretely

The `karpathy/tinyllamas` Hugging Face repo lists the actual files
([tree listing](https://huggingface.co/karpathy/tinyllamas/tree/main/stories260K)):
`stories260K.bin` at 1.06 MB (float32), and a matching custom tokenizer `tok512.bin` at 6.23 kB
covering a 512-token vocabulary (not the full 32000-token Llama tokenizer). The model's
published config, from the llama2.c README table and confirmed independently by a third-party
embedded port (below), is `dim=64, hidden_dim=172, n_layers=5, n_heads=8, n_kv_heads=4,
seq_len=512, vocab_size=512`
([llama2.c README](https://github.com/karpathy/llama2.c/blob/master/README.md),
[doc/stories260K.md](https://github.com/karpathy/llama2.c/blob/master/doc/stories260K.md)).
This matches the user-stated "about 1 MB of float32 weights."

Int8 quantization: the README documents `export.py`'s `version 2` format as "Q8_0" (llama.cpp
terminology, symmetric range [-127,127]) and states a measured "3X speedup while reducing the
checkpoint size by 4X" (4.6 tok/s float32 to 14 tok/s int8, on a 7B model on a 96-thread Linux
box -- the ratio, not the absolute numbers, is the transferable fact). 1.06 MB / 4 matches the
user-stated "about 260 KB" for the int8 form.

### A real embedded port with hard numbers: EmbedLlama on STM32H7A3

[schuhandreas/embedllama-stm32h7a3](https://github.com/schuhandreas/embedllama-stm32h7a3) runs
exactly the stories260K checkpoint (`dim=64, hidden_dim=172, n_layers=5`, confirmed) on an
STM32H7A3ZI Cortex-M7 (hardware FPU, fpv5-d16) with about 1.4 MB RAM and 2 MB flash. Its own
numbers, quoted from the repo:

- Weights stay in flash as a linked `.rodata` object read in place through pointers -- it does
  not copy the checkpoint into RAM, i.e. it avoids exactly the `mmap` step that breaks on this
  device, by not needing mmap semantics at all (flash is directly addressable on the STM32H7,
  unlike the RP2040's Dhara-mediated flash).
- **RAM breakdown: about 640 KB for the KV cache alone**, about 20 KB for activations, about
  132 KB for tokenizer tables. Total is roughly 800 KB, an order of magnitude over this
  device's entire 96 KB process ceiling, let alone its 264 KB of total SRAM.
- Throughput: 6.5 tok/s at 64 MHz in a debug build, 75 tok/s at 280 MHz in a release build, and
  87 tok/s at 280 MHz with CMSIS-DSP-accelerated kernels (all figures include UART output
  overhead) -- all with a hardware FPU.

The 640 KB KV-cache figure is arithmetically exact:
`n_layers * seq_len * kv_dim * 4 bytes * 2 (K and V) = 5 * 512 * 32 * 4 * 2 = 655,360 bytes`,
where `kv_dim = dim * n_kv_heads / n_heads = 64 * 4 / 8 = 32`. **This term scales linearly with
`seq_len` and is independent of quantizing the weights** -- it is float32 activation state, not
weight storage, and int8-quantizing weights does nothing to shrink it.

### A real RP-family port with hard numbers: PicoLlama (RP2350, not RP2040)

[earlephilhower/PicoLlama](https://github.com/earlephilhower/PicoLlama), MIT licensed, targets
the **RP2350** (Cortex-M33, has an FPU), explicitly "for use only with arduino-pico due to the
need for RP2350 and PSRAM support," and states "this library needs 16MB of flash and 8MB of
PSRAM." Its one reported number: "each token ... takes around 850ms to generate," about
1.18 tok/s, which the author attributes to being QSPI bandwidth/latency-bound, not
compute-bound. This is the closest published RP-family data point, and it already needs
16 MB flash and 8 MB external PSRAM -- both absent on this device (2 MB flash, no PSRAM, only
264 KB SRAM). No RP2040 (Cortex-M0+, no FPU) llama2.c benchmark was found in this search.

### A second useful anchor: llama4micro (Cortex-M7, FPU, 64 MB RAM)

[maxbbraun/llama4micro](https://github.com/maxbbraun/llama4micro), MIT licensed, runs a
tinyllamas/TinyStories checkpoint on a Coral Dev Board Micro's 800 MHz Cortex-M7 (hardware
FPU) with 64 MB RAM, reporting "~2.5 tokens per second." Combined with the STM32H7A3 numbers
above, every published llama2.c-family microcontroller port that has hard numbers runs on a
Cortex-M7-class core with a hardware FPU. None has been published for a Cortex-M0+.

### Does it fit in 96 KB on this device? No, not as published, on two independent axes

**RAM.** The KV cache for stories260K at its trained `seq_len=512` is about 640 KB of float32
activations by itself -- roughly 6.7x the entire 96 KB process budget, before counting weights,
tokenizer tables, or the interpreter's own stack and code. This is the dominant, disqualifying
term, and it is a property of the model's context length, not of weight quantization. Cutting
`seq_len` at inference time (which llama2.c's `RunState` allocation supports, since it is sized
by `p->seq_len` at `malloc_run_state()` time and the model's absolute max is 512, not a hard
lower bound) shrinks the KV cache linearly: `seq_len=64` gives `5*64*32*4*2 = 81,920 bytes`
(~80 KB) -- still consumes essentially the whole 96 KB budget on its own, leaving nothing for
weights-in-flight, tokenizer tables, or activation scratch (`hb`/`hb2` at `hidden_dim=172`
floats each add another ~1.4 KB, `att` at `n_heads*seq_len` adds a few KB more).
`seq_len=16-32` (`~20-40 KB` of KV cache) is closer to something that could coexist with a few
tens of KB of streamed-in weight tiles and interpreter overhead, but that is a materially
different, much-shorter-context model than the published stories260K checkpoint, and no one
has published tok/s numbers at that context length on any platform, let alone this one.

**Flash.** The root filesystem on this device has about 120 KB free. The float32 stories260K
checkpoint (1.06 MB) does not fit by roughly 9x. Its int8-quantized form (about 260 KB by the
README's stated 4x reduction) still does not fit by roughly 2x. The full 2 MB of flash has
headroom in principle (the filesystem currently uses only about 1 MB of the 2 MB total), but
the fixed facts of this device specify the *root filesystem's free space* as the reachable
budget for a file read via the Dhara-mediated `read(2)` path; fitting the int8 model requires
either shrinking something else in the current ~1 MB filesystem allocation by ~140 KB, growing
the filesystem into the flash region not currently assigned to it, or storing the model as a
raw partition read through a separate device node outside the DiscoBSD filesystem -- none of
which is "fits as-is."

**Speed.** No RP2040 (Cortex-M0+, no FPU) number exists in the literature. The two closest
anchors both have hardware FPUs and are far faster clocks/cores: the STM32H7A3 gets 6.5 tok/s
*at 64 MHz in an unoptimized debug build with a hardware FPU*; PicoLlama on the FPU-equipped
RP2350 gets 1.18 tok/s but is bandwidth-bound, not compute-bound. The RP2040 is clocked close
to the STM32H7A3's debug-mode clock (125 MHz vs. 64 MHz) but has no FPU at all -- every `sqrtf`,
`expf`, `powf`, `cosf`, `sinf`, and every float multiply-accumulate in the matmuls, becomes a
software-emulated routine executing tens of Thumb-1 instructions in place of one FPU
instruction. Softfloat-vs-hardware-FPU slowdowns of one to two orders of magnitude are typical
on Cortex-M0 class cores for float-heavy workloads. A defensible order-of-magnitude estimate,
not a measurement, is well under 1 tok/s for stories260K even at a drastically shortened
context, and this estimate has not been validated against any published data -- it is inferred,
not confirmed, and should be labeled as such if repeated.

### What would have to change

1. **Drop `mmap` entirely.** Replace it with a layer-streaming reader that `open()`s the
   weights file once and issues `read(2)` (or `pread`) calls to pull in exactly the tensor
   slice needed for the layer currently being computed, discarding it once consumed. This
   matches the device's actual flash access path (Dhara over QSPI, no XIP mapping into user
   address space) and is the single largest correctness fix required, independent of size.
2. **Quantize to int8 (Q8_0) and stream one layer's weights at a time**, never materializing
   the whole checkpoint in RAM. One layer of stories260K (`hidden_dim=172, dim=64`) is on the
   order of tens of KB in int8, which is RAM-feasible in isolation; the disqualifying cost is
   the KV cache, not the weights.
3. **Shrink `seq_len`** (retrain or re-export the checkpoint, or simply cap the runtime context
   at inference time) to on the order of 16-32 tokens to bring the KV cache under the 96 KB
   ceiling alongside everything else. This changes what the model actually is -- a much
   shorter-context toy -- not just how it is packaged.
4. **Fit the model file in flash**: either free ~140 KB in the existing ~1 MB filesystem
   allocation, grow the filesystem into the flash headroom, or read it from a separate raw
   flash region outside the DiscoBSD root filesystem.
5. Accept sub-1-tok/s throughput as the likely outcome (unverified estimate) given the absence
   of an FPU, or scope the ambition down to something smaller than stories260K's default
   configuration (fewer layers, smaller `hidden_dim`, or a purpose-trained tinier checkpoint) if
   a specific tok/s target must be hit.
6. **stories15M does not fit under any of these changes.** Its file sizes (60 MB float32, about
   15 MB int8, per the 4x rule) exceed the entire 2 MB flash chip by roughly an order of
   magnitude even int8-quantized; it is disqualified by flash capacity alone before RAM or
   speed are considered.

## Part 2: a native Thumb-1 toolchain for this board

DiscoBSD's existing tree (cc/ccom = PCC with `arch-mips` and an `arch-arm` directory, cpp, a
MIPS `as`, a MIPS `ld`, smallc, smlrc/Smaller C with a MIPS backend, lcc, a MIPS `adb`) emits or
handles MIPS throughout. The goal is a Thumb-1 (ARMv6-M) equivalent that runs *on the device
itself*, within the 96 KB per-process ceiling -- this is a self-hosting requirement, not a
cross-compiler requirement, matching the precedent that RetroBSD/DiscoBSD's MIPS toolchain
already self-hosts on comparably small PIC32 targets.

### (a) An assembler for Thumb-1 emitting a.out

**PCC's own `arch-arm` assumes full 32-bit ARM, not Thumb.** Fetching
[IanHarvey/pcc `arch/arm/macdefs.h`](https://raw.githubusercontent.com/IanHarvey/pcc/master/arch/arm/macdefs.h)
directly shows `#define MAXREGS 34`, all 16 general registers R0-R15 (with `SL`/`FP`/`IP`/
`SP`/`LR`/`PC` aliases per APCS), *plus* floating-point registers F0-F7 and feature flags
`FEATURE_FPA` / `FEATURE_VFP` / `FEATURE_HARDFLOAT`. Thumb is not mentioned anywhere in the
file. PCC's ARM backend targets a 16-register, hardware-FP-aware, conditionally-executed
32-bit ISA -- essentially the opposite of ARMv6-M's 8-low-register, no-FPU, mostly
unconditional 16-bit Thumb-1 subset. Retrofitting `arch-arm` for Thumb-1 means rewriting
`macdefs.h`'s register model, `local2.c`'s instruction emission, and `table.c`'s pattern rules
from scratch; it is not a small patch, it is a new backend wearing the old directory's name.

**The Fuzix Compiler Kit and Fuzix-Bintools have no ARM backend at all.**
[EtchedPixels/Fuzix-Compiler-Kit](https://github.com/EtchedPixels/Fuzix-Compiler-Kit) (GPLv3,
optimizer under the Clarified Artistic License) targets 6502, 6800, 6809, 8080/8085, Z80,
65C816, 8086, and several experimental 16-bit CPUs -- no ARM/Thumb variant exists in its
backend list. [EtchedPixels/Fuzix-Bintools](https://github.com/EtchedPixels/Fuzix-Bintools)
(derived from Mark Williams Company code, now archived read-only as of December 23, 2025)
likewise has no ARM backend among its assembler/linker targets (1802, 6502, 6800, 6809, 8008,
8080, 9900, Gameboy, HC11, Z8, Z80, plus test/early-work targets). The FUZIX OS wiki does list
"work in progress ports to armm0 (Raspberry Pi PICO) and armm4"
([FUZIX wiki](https://github.com/EtchedPixels/FUZIX/wiki)), but that port targets the kernel
build via GCC, not the Fuzix Compiler Kit's own compiler/assembler/linker -- it does not supply
a small self-hosting Thumb assembler either.

**vbcc** has ARM backends historically used for GBA/embedded ARM homebrew, but no evidence was
found in this search confirming a vbcc backend emits Thumb-1 (as opposed to full ARM) machine
code, and vbcc's ARM work targets cross-compilation from a desktop host, not self-hosting in
96 KB. Not a fit as researched.

**The Amsterdam Compiler Kit (ACK)** lists an `arm` backend directory among its many small-CPU
targets ([Amsterdam Compiler Kit](https://en.wikipedia.org/wiki/Amsterdam_Compiler_Kit),
[tack.sourceforge.net](https://tack.sourceforge.net/)), but no detail on whether that backend
targets Thumb or full ARM was found, and ACK's EM (Encoding Machine) intermediate-code model
adds a full extra translation stage (EM to target) that is a heavier build than a direct
one-pass compiler for a 96 KB self-hosting target.

**Conclusion for (a): no existing small a.out-emitting Thumb-1 assembler was found anywhere in
this search.** The shortest path is to write one. Thumb-1 is a strong candidate for a from-
scratch tool because it is a genuinely small ISA: 16-bit fixed-width instructions, roughly 50
opcodes, only 8 directly addressable low registers for most operations, and a much smaller
addressing-mode/encoding matrix than MIPS (which DiscoBSD's existing `as` already handles) or
full ARM. A two-pass, table-driven Thumb-1 assembler emitting the same a.out object format
DiscoBSD's MIPS `as`/`ld` already produce is plausibly comparable in size to the small
architecture-specific `as1-*` modules already living in Fuzix-Bintools for 8/16-bit CPUs --
i.e., a few thousand lines, not a multi-architecture GNU-binutils-scale undertaking. This
directly reuses the a.out struct layout and relocation model DiscoBSD's linker already
understands, and needs only Thumb-specific relocation types (PC-relative branch/call encoding,
the `BL`/`BLX` 22-bit split-immediate long-branch sequence) added on top.

### (b) A linker for a.out on the target

DiscoBSD already carries a MIPS `ld` that emits a.out. The cheapest path is not a new linker
but reuse: retarget the existing a.out linker's relocation-application switch to understand
Thumb-1 relocation types (a handful: 16-bit PC-relative branch, the two-instruction long-branch
pair, absolute data words) while keeping its symbol table, section layout, and a.out header
handling untouched. This is a much smaller job than the assembler, because a.out linking is
largely relocation-type-driven and Thumb-1 has few relocation kinds compared to MIPS's
delay-slot-aware, `%hi`/`%lo`-split relocations (SmallerC's own `cgmips.c` needs a
`REORDER_WORKAROUND` flag specifically to paper over RetroBSD assembler delay-slot reordering,
per the file itself -- Thumb-1 has no delay slots and does not need an equivalent).

### (c) Smaller C's backend structure and a cgthumb.c estimate

Fetching `cgmips.c` and `cgx86.c` directly from
[alexfru/SmallerC `v0100`](https://github.com/alexfru/SmallerC/tree/master/v0100) (BSD
licensed) shows the MIPS backend (`cgmips.c`) at roughly 1,450-1,500 lines versus the x86
backend (`cgx86.c`) at roughly 2,500-2,800 lines -- the MIPS backend is smaller because it
targets a single clean 32-bit load/store RISC ISA with one calling convention, while `cgx86.c`
carries multiple x86 memory models (tiny/small/huge/unreal) and both 16- and 32-bit output.
`cgmips.c` documents its own assumptions inline: `SizeOfWord = 4`, the standard MIPS register
set (`zero, at, v0-v1, a0-a3, t0-t9, s0-s7, sp, fp, ra`), parameter passing through `a0`-`a3`
for the first four arguments, and conditional handling for whether hardware `seb`/`seh`
sign-extend instructions exist. Confirmed via
[smlrc.md](https://github.com/alexfru/SmallerC/blob/master/v0100/doc/smlrc.md) and web search:
Smaller C's RetroBSD/MIPS target does not assemble internally -- it emits MIPS assembly text
consumed by **RetroBSD's own external `as` and `ld`**, with a separate preprocessor, library,
and driver for that target. This matters directly for effort estimation: building a `cgthumb.c`
solves only the *compiler* half of the chain; it still needs the Thumb-1 assembler and
relinked `ld` from (a) and (b) to produce a runnable binary.

A `cgthumb.c` estimate: Thumb-1's register file (8 low registers for general operations, `sp`/
`lr`/`pc` special-purposed) is more constrained than MIPS's flatter 32-register file, which
adds register-pressure/spill-code complexity `cgmips.c` does not have to solve, but Thumb-1's
addressing modes and instruction count are smaller than MIPS's, and it has no delay slots to
work around. A reasonable estimate is a `cgthumb.c` in the same order of magnitude as
`cgmips.c`, roughly 1,500-2,500 lines, higher rather than lower given the extra register-
allocation work -- this is an estimate reasoned from the two existing backends' relative
complexity, not a measurement, and should be labeled as such.

### (d) PCC arch-arm's ISA assumptions, restated

Already covered in (a): `arch/arm/macdefs.h` assumes the full 16-register, hardware-FP-capable
32-bit ARM ISA (`MAXREGS 34` including F0-F7 and `FEATURE_FPA`/`FEATURE_VFP`), with no Thumb
awareness. PCC is a two-pass, multi-target compiler (frontend plus per-architecture
`local.c`/`local2.c`/`order.c`/`table.c`/`code.c`/`macdefs.h` files) designed to be portable
across many targets, but retargeting `arch-arm` specifically for Thumb-1 means replacing every
one of those six files' register model and instruction selection rules -- not a smaller lift
than writing a fresh backend, and PCC's general architecture (multiple compilation passes,
larger internal tables for a broader C dialect) is also a heavier RAM footprint to self-host
in 96 KB than Smaller C's single-pass design, though no direct RAM measurement of PCC's
runtime footprint was found in this search to confirm that by number.

### Ranking by effort, and the imperative path for the top choice

1. **Smaller C + a new Thumb-1 `as`/`ld` (lowest effort).** Smaller C is BSD licensed, single-
   pass, already has a MIPS backend of known, modest size (~1,500 lines) as a direct structural
   template, and DiscoBSD already has a working a.out linker to retarget rather than write from
   scratch. The dominant unknown is the assembler, and Thumb-1's small, regular 16-bit encoding
   makes a from-scratch tool tractable.
2. **PCC arch-arm retargeted for Thumb-1 (medium-high effort).** Already in DiscoBSD's tree by
   name, but its register/FP model is fundamentally wrong for Thumb-1 and every backend file
   needs rewriting; PCC's heavier multi-pass design is also a less certain fit for a 96 KB
   self-hosting budget.
3. **Fuzix Compiler Kit (blocked).** GPLv3-licensed, well-proven on many small CPUs, but has no
   ARM/Thumb backend today, and the one Pico-adjacent FUZIX effort (`armm0`) targets GCC for
   the kernel build, not this kit's own compiler chain -- would-be work starts from zero on the
   backend just like option 1, without Smaller C's smaller/simpler MIPS-backend precedent to
   copy, and carries copyleft licensing DiscoBSD's existing BSD/permissive tools do not.
4. **vbcc or ACK (least certain).** Neither was confirmed in this search to have a working,
   small-footprint Thumb-1-emitting, a.out-producing, self-hostable path; both would need
   further primary-source investigation before ranking above the others.

Imperative steps for path 1:

1. Write a minimal two-pass Thumb-1 assembler (`as`) from scratch: fixed 16-bit instruction
   table (~50 opcodes), 8-low-register operand model, symbol table, and a.out object emission
   matching the struct layout DiscoBSD's existing MIPS `as` already writes. Handle only the
   relocation kinds Thumb-1 needs: PC-relative conditional/unconditional branch, the `BL` 22-
   bit split-immediate long-call sequence, and absolute data-word relocations.
2. Retarget DiscoBSD's existing a.out `ld` to apply those Thumb-1 relocation types alongside
   its current MIPS ones; leave symbol-table, section-layout, and a.out-header logic
   untouched.
3. Copy `v0100/cgmips.c` from [alexfru/SmallerC](https://github.com/alexfru/SmallerC) as the
   structural template for a new `cgthumb.c`: replace the MIPS register set and calling
   convention with Thumb-1's 8 low registers and the standard `r0`-`r3` argument-passing
   convention, replace MIPS instruction emission with Thumb-1 mnemonics, and drop the
   `REORDER_WORKAROUND` delay-slot logic entirely (Thumb-1 has no delay slots).
4. Wire the new `cgthumb.c` into Smaller C's driver as a `THUMB` build macro paralleling the
   existing `MIPS` macro, emitting assembly text for the new `as` from step 1.
5. Bring up libm's `sqrtf`/`expf`/`powf`/`sinf`/`cosf` (needed independently for Part 1's
   inference workload) as plain C compiled by this same chain, since Thumb-1 has no hardware
   FPU or divide to delegate to.
6. Build and run the whole chain (`cc`/`cpp`/`as`/`ld`) self-hosted on the device last, after
   cross-compiling and validating it on a desktop host first; verify each tool's own resident
   size stays under the 96 KB process ceiling before attempting self-hosted operation, since
   that ceiling -- not ISA complexity -- is the binding constraint an assembler or compiler
   over roughly 80 KB of runtime RAM cannot clear on this device.

## Sources

- [karpathy/llama2.c](https://github.com/karpathy/llama2.c) -- README (MIT license, stories260K
  table, Q8_0 quantization, 4.6/14 tok/s numbers) and
  [run.c](https://raw.githubusercontent.com/karpathy/llama2.c/master/run.c) (mmap-only weight
  loading, calloc'd RunState, libm calls)
- [doc/stories260K.md](https://github.com/karpathy/llama2.c/blob/master/doc/stories260K.md)
- [karpathy/tinyllamas, stories260K](https://huggingface.co/karpathy/tinyllamas/tree/main/stories260K)
  -- file sizes (stories260K.bin 1.06 MB, tok512.bin 6.23 kB)
- [exo-explore/llama98.c](https://github.com/exo-explore/llama98.c),
  [a43501/llama98.c](https://github.com/a43501/llama98.c) -- Windows 98 port, not "llama89.c"
- [yl01inve/llama2.c_dos](https://github.com/yl01inve/llama2.c_dos) -- C89 DOS build
  (`gcc -std=c89`)
- [yeokm1/dosllam2](https://github.com/yeokm1/dosllam2) -- DOS port
- [schuhandreas/embedllama-stm32h7a3](https://github.com/schuhandreas/embedllama-stm32h7a3) --
  MIT licensed; STM32H7A3 (Cortex-M7, FPU); stories260K config confirmed
  (dim=64, hidden_dim=172, n_layers=5); ~640 KB KV cache, ~20 KB activations, ~132 KB tokenizer
  tables; 6.5/75/87 tok/s at 64/280/280(+CMSIS-DSP) MHz
- [earlephilhower/PicoLlama](https://github.com/earlephilhower/PicoLlama) -- MIT licensed;
  targets RP2350 (Cortex-M33, FPU) with PSRAM, not RP2040; needs 16 MB flash, 8 MB PSRAM;
  ~850 ms/token (~1.18 tok/s), bandwidth-bound
- [maxbbraun/llama4micro](https://github.com/maxbbraun/llama4micro) -- MIT licensed; Coral Dev
  Board Micro, 800 MHz Cortex-M7 (FPU), 64 MB RAM; ~2.5 tok/s
- [EtchedPixels/Fuzix-Compiler-Kit](https://github.com/EtchedPixels/Fuzix-Compiler-Kit) --
  GPLv3 (optimizer under Clarified Artistic License); target list has no ARM/Thumb backend
- [EtchedPixels/Fuzix-Bintools](https://github.com/EtchedPixels/Fuzix-Bintools) -- derived from
  Mark Williams Company code; archived December 23, 2025; no ARM backend
- [EtchedPixels/FUZIX wiki](https://github.com/EtchedPixels/FUZIX/wiki) -- "work in progress
  ports to armm0 (Raspberry Pi PICO) and armm4," targets GCC for the kernel, not this kit's own
  compiler chain
- [IanHarvey/pcc, arch/arm/macdefs.h](https://raw.githubusercontent.com/IanHarvey/pcc/master/arch/arm/macdefs.h)
  -- MAXREGS 34, full ARM register set including F0-F7 and FEATURE_FPA/FEATURE_VFP, no Thumb
  mention
- [IanHarvey/pcc, arch/ listing](https://github.com/IanHarvey/pcc/tree/master/arch) -- confirms
  an `arm` directory (code.c, local.c, local2.c, macdefs.h, order.c, table.c) targeting full ARM
- [alexfru/SmallerC](https://github.com/alexfru/SmallerC) -- BSD licensed;
  [cgmips.c](https://raw.githubusercontent.com/alexfru/SmallerC/master/v0100/cgmips.c)
  (~1,450-1,500 lines, MIPS register set, REORDER_WORKAROUND delay-slot flag) versus
  [cgx86.c](https://raw.githubusercontent.com/alexfru/SmallerC/master/v0100/cgx86.c)
  (~2,500-2,800 lines); [smlrc.md](https://github.com/alexfru/SmallerC/blob/master/v0100/doc/smlrc.md)
  (MIPS target emits assembly for RetroBSD's own external as/ld, not internal assembly)
- [Amsterdam Compiler Kit](https://en.wikipedia.org/wiki/Amsterdam_Compiler_Kit),
  [tack.sourceforge.net](https://tack.sourceforge.net/) -- lists an `arm` backend directory,
  Thumb-vs-full-ARM status not confirmed in this search
