# A femtoLLM for DiscoBSD/RP2040: what fits in 25 KB, and what to build

Scope: the target is a Raspberry Pi Pico running DiscoBSD on the RP2040 (dual Cortex-M0+,
ARMv6-M, Thumb-1 only, no FPU, no hardware divide, 125 MHz), 264 KB SRAM total with a 96 KB
per-process ceiling for text+data+bss+stack, a.out executables loaded whole into RAM, and a
2 MB QSPI flash reached through a Dhara FTL (a user process reads it with `read(2)`, not
`mmap`). The design budget asked for here is CODE + WEIGHTS in roughly 25 KB, running inside
the 96 KB window with integer or fixed-point math only. This document surveys existing tiny
text/token predictors against that budget, quantifies what a 25 KB char-level model can hold,
and recommends a concrete, buildable design.

`sys/arch/rp2040/doc/research/llama89-and-toolchain.md` already establishes, with primary
sources, that Karpathy's `llama2.c` transformer family (stories260K and up) does not fit this
device at any published configuration: the KV cache alone is ~640 KB of float32 at the trained
`seq_len=512`, roughly 6.7x the entire 96 KB budget, and every published llama2.c microcontroller
port with hard numbers runs on a Cortex-M7-class core with a hardware FPU. That document is the
authority on the transformer question; this one starts from its "not a transformer" conclusion
and works out what a non-transformer predictor can do at 25 KB.

## 1. Existing projects, sizes, and feasibility verdicts

| Project | Size | Verdict at 25 KB / 96 KB / no-FPU / Thumb-1 |
|---|---|---|
| **llama2.c stories260K** (Karpathy, MIT) -- dim=64, hidden_dim=172, n_layers=5, n_heads=8, n_kv_heads=4, seq_len=512, vocab=512, ~260K params | 1.06 MB fp32 weights, ~260 KB int8 (Q8_0), tokenizer 6.2 KB. KV cache at seq_len=512 is ~640 KB of float32 activations. | Infeasible. Weights alone are 10x the whole 25 KB budget even int8-quantized, and the KV cache is 6.7x the entire 96 KB process ceiling regardless of weight quantization. Shrinking `seq_len` to 16-32 tokens gets the KV cache to ~20-40 KB but leaves a materially different, much-shorter-context model with no weight budget left. [github.com/karpathy/llama2.c](https://github.com/karpathy/llama2.c), [doc/stories260K.md](https://github.com/karpathy/llama2.c/blob/master/doc/stories260K.md), [huggingface.co/karpathy/tinyllamas](https://huggingface.co/karpathy/tinyllamas/tree/main/stories260K) |
| **llama2.c stories15M** | 60 MB fp32, ~15 MB int8 | Infeasible by an order of magnitude against the entire 2 MB flash chip, before RAM or FPU questions arise. |
| **EmbedLlama on STM32H7A3** (schuhandreas, MIT) -- stories260K on Cortex-M7 | Same stories260K weights; ~640 KB KV cache, ~20 KB activations, ~132 KB tokenizer tables in RAM; runs at 6.5-87 tok/s | Infeasible on this device: it needs a hardware FPU (fpv5-d16) the RP2040 does not have, and ~800 KB total RAM against a 96 KB ceiling. Useful only as an anchor for what stories260K actually costs. [github.com/schuhandreas/embedllama-stm32h7a3](https://github.com/schuhandreas/embedllama-stm32h7a3) |
| **On-Chip LM (Hackaday project 205074)** -- same stories260K/STM32H7A3 pairing | ~87 tok/s @ 280 MHz with CMSIS-DSP | Same verdict as above; confirms the FPU dependency and RAM cost independently. [hackaday.io/project/205074](https://hackaday.io/project/205074/instructions) |
| **PicoLlama** (earlephilhower, MIT) -- RP2350 (Cortex-M33, has an FPU) + PSRAM | needs 16 MB flash, 8 MB PSRAM; ~1.18 tok/s, bandwidth-bound | Infeasible: different silicon (RP2350, not RP2040), external PSRAM this device does not have, and still an order of magnitude over the flash budget. [github.com/earlephilhower/PicoLlama](https://github.com/earlephilhower/PicoLlama) |
| **llama4micro** (maxbbraun, MIT) -- Coral Dev Board Micro, Cortex-M7 (FPU), 64 MB RAM | ~2.5 tok/s | Infeasible: 64 MB RAM is 680x this device's total SRAM, and again depends on a hardware FPU. [github.com/maxbbraun/llama4micro](https://github.com/maxbbraun/llama4micro) |
| **llm.c** (Karpathy, MIT) -- full GPT-2 training in raw C/CUDA | GPT-2-small is 124M parameters, ~500 MB fp32 | Infeasible; it is a training harness for a mid-size transformer on GPU/CPU hosts, not an embedded inference target. Useful only as a reading reference for a C-only forward pass. [github.com/karpathy/llm.c](https://github.com/karpathy/llm.c) |
| **nanoGPT** (Karpathy, MIT) -- PyTorch educational GPT trainer | smallest configs still MB-scale in fp32 | Infeasible as shipped; it is a PyTorch training script, not a standalone C inference engine, and has no path to a 25 KB static footprint without a full rewrite (which is what llama2.c already is). [github.com/karpathy/nanoGPT](https://github.com/karpathy/nanoGPT) |
| **femtoGPT** (keyvank, MIT, pure Rust) -- minimal from-scratch GPT, CPU/GPU training+inference | no microcontroller port found; depends on Rust's allocator and float tensor ops throughout | Infeasible without a from-scratch reimplementation; it is architecturally the same transformer-plus-KV-cache shape as llama2.c, so the same KV-cache argument in the linked document applies. Useful as a second confirmation that "GPT" in this size class means "hundreds of KB to MB," not 25 KB. [github.com/keyvank/femtoGPT](https://github.com/keyvank/femtoGPT) |
| **char-rnn / min-char-rnn.py** (Karpathy) -- vanilla RNN, char-level, numpy, ~100 lines | model itself is only as large as its hidden-size^2 weight matrix (a few KB to a few hundred KB depending on hidden size); the *reference* implementation needs Python/numpy and float BPTT training, not present on-device | Feasible **as an architecture reference**, not as code: a hidden size of 32-64 with a ~96-symbol alphabet is small enough to fit a 25 KB int8 budget (see Section 2), but the recurrent state update (`tanh` of a matrix-vector product every step) needs the same fixed-point/LUT treatment as the MLP design in Section 3. License note: the main `karpathy/char-rnn` Torch/Lua repository's own LICENSE terms were not confirmed by primary source in this search; do not import its code without checking that file directly before reuse -- reimplement the small numpy reference's *algorithm*, which is public and widely reproduced, rather than copying any specific repository's text. [github.com/karpathy/char-rnn](https://github.com/karpathy/char-rnn), [gist.github.com/karpathy/d4dee566867f8291f086](https://gist.github.com/karpathy/d4dee566867f8291f086) |
| **TensorFlow Lite Micro** (Apache 2.0) | interpreter core alone is ~16 KB on a Cortex-M3-class target; Pete Warden's original announcement cites "only another 25KB of Flash" for the TFLite code plus "30KB of RAM" for a wake-word-class model, i.e. the *interpreter framework* consumes most or all of a 25 KB budget before any model weights are counted | Infeasible for a 25 KB all-in budget: the interpreter, op-resolver tables, and C++ runtime overhead alone are comparable to or larger than the entire budget this project has for code *and* weights combined. TFLM is designed for MCUs with tens to hundreds of KB free specifically for the framework, which is a different budget class than this project's. It is also not evaluated on Cortex-M0+/Thumb-1 without a hardware FPU in the sources found here. [petewarden.com, "Launching TensorFlow Lite for Microcontrollers"](https://petewarden.com/2019/03/07/launching-tensorflow-lite-for-microcontrollers/), [github.com/tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro) |
| **Classic n-gram / Markov char models** | a symbol table keyed by a k-character context mapping to next-symbol counts; size scales with `(number of distinct contexts) x (alphabet size)`, not with a fixed hidden-layer size, and is tunable directly by hashing contexts into a fixed-size table | **Feasible, and the strongest lower-risk candidate.** No float anywhere -- it is integer counts and integer division/shifts. A hashed order-2 or order-3 context table over a 96-symbol alphabet fits comfortably in an int8/int16 count array sized to the remaining budget, at the cost of the classic Markov weaknesses (only ever reproduces sequences seen verbatim in training, no generalization to unseen contexts beyond backing off to a shorter order). [cs.princeton.edu Markov Model of Natural Language](https://www.cs.princeton.edu/courses/archive/spring11/cos126/assignments/markov.html) |
| **PPM (Prediction by Partial Matching)** | Fenwick's "PPM compression without escapes" reports PPM reaching as little as ~2.2 bits/char on English text; a byte-oriented PPM predictor with escape handling and multiple context orders needs "a few hundred kilobytes of data space" in Fenwick's own description for a full-strength implementation | Feasible only in a stripped-down, low-order form. A full multi-order PPM with escape/exclusion logic (the versions that hit 2.2 bpc) is sized for "a few hundred KB," an order of magnitude over this budget; a fixed single-order (order-2/3) PPM-like model collapses to the same hashed-Markov design above once the escape mechanism and multi-order blending are cut down to fit 25 KB. Treat PPM as the *quality ceiling* this project is not budgeted to reach, and the Markov model as PPM's low-order, low-cost subset. [Fenwick, "PPM compression without escapes"](https://www.cs.auckland.ac.nz/~peter-f/FTPfiles/PPMnoEsc.pdf), [arxiv.org/pdf/1008.5078 "Prediction by Compression"](https://arxiv.org/pdf/1008.5078) |

## 2. The realistic architecture at 25 KB

A transformer is not feasible at this budget on this device, for two independent reasons
established above and in the linked toolchain document: the KV cache scales with context length
and is float32 by construction in every published implementation, and it alone exceeds the
entire 96 KB process ceiling for any context length long enough to be interesting; and the
attention/RoPE/RMSNorm math (`sqrtf`, `expf`, `powf`, `sinf`, `cosf`) has no hardware path on a
Cortex-M0+ and would need software-emulated float on every element of every layer. The 25 KB
budget forces a choice between:

- a hashed **n-gram / Markov** context table (no learned parameters, no activation functions,
  pure integer counts), or
- a small **MLP** (a few dense layers over a short window of embedded input symbols, quantized
  to int8/fixed-point, with LUT-based nonlinearities), or
- a small **char-RNN** (one recurrent hidden state updated by a `tanh`-gated matrix-vector
  product each step), which is architecturally the MLP case with the hidden state fed back as
  part of next step's input, at the same per-step weight cost.

All three are integer/fixed-point-only designs at this size; none needs a transformer's
attention mechanism or KV cache.

### Sizing a char-level MLP over ~96 printable ASCII symbols

Let vocabulary `V = 96` (the printable ASCII range), embedding dimension `E`, context window
`k` characters of history, hidden layer width `H`, weights stored as int8 (1 byte/weight), and
biases as int16 (2 bytes/bias). The weight cost is:

- Embedding table: `V * E` bytes
- Input-to-hidden layer: `(k * E) * H` bytes of weights + `2H` bytes of bias
- Hidden-to-output layer: `H * V` bytes of weights + `2V` bytes of bias

Three concrete points inside a 25 KB code+weight budget (leaving headroom for code):

| E | k | H | Embedding | Input layer | Output layer | Total weights |
|---|---|---|---|---|---|---|
| 8 | 3 | 64 | 768 B | 1,664 B | 6,432 B | ~8.9 KB |
| 8 | 3 | 128 | 768 B | 3,328 B | 12,480 B | ~16.6 KB |
| 16 | 4 | 128 | 1,536 B | 8,448 B | 12,480 B | ~22.5 KB |

The middle row (`E=8, k=3, H=128`, ~16.6 KB of weights) leaves roughly 8 KB for code, which is
generous for an integer matmul-plus-LUT forward pass (Section 3 sizes the actual code at 2-4 KB).
The bottom row is close to the full 25 KB and should be treated as the stretch configuration,
not the default target.

### Quality: what bits-per-character is realistic

Character-level compression literature is the closest available proxy for a next-char
predictor's quality, since bits-per-character (bpc) directly measures how confidently the model
assigns probability to the true next symbol -- the same quantity a predictor's cross-entropy
loss reports. Multi-order PPM with escape/exclusion logic reaches ~2.2 bpc on English text at a
cost of "a few hundred KB" of context tables (Fenwick, above) -- this is the quality ceiling for
classical predictors and is out of this budget's reach. A low-order (order-2/3) hashed Markov
model or a small MLP/char-RNN in the sizes above should be expected in the 2.5-3.5 bpc range: well
above a uniform-random baseline (`log2(96) ~ 6.58` bpc) and capable of reproducing common short
words, digraphs, and local structure (e.g. correctly completing "th" -> "e", closing a quote or
parenthesis it opened), but not capable of long-range coherence or novel sentence structure a
much larger model would produce. This is an estimate reasoned from the cited compression
literature and the model-size table above, not a measurement on this project's own corpus; it
should be verified empirically once a host-side trainer exists (Section 3).

### The no-FPU constraint: fixed-point and lookup tables

Every weight and activation is integer. A workable scheme:

- **Weights**: int8, with a single shared per-layer scale factor (a power-of-two shift, so
  dequantization is `value >> shift`, not a float multiply) -- the same "Q8_0"-style symmetric
  int8 quantization llama2.c's own `export.py` uses for its checkpoints, minus the floating-point
  dequantization step at inference time.
- **Accumulation**: int32 (each dot product of up to a few hundred int8 x int8 terms fits well
  inside int32 without overflow).
- **`tanh` (or `sigmoid`) activation**: a fixed 256- or 512-entry `static const int16_t` lookup
  table indexed by the clamped, right-shifted accumulator value -- table cost is 512 or 1024
  bytes, negligible against the weight budget above, and O(1) per activation with no float
  instruction ever executed.
- **Softmax / sampling**: an exact float softmax is not attempted. Two integer-only
  alternatives fit the budget: (a) skip softmax entirely and sample by scanning the int32 logits
  for an integer-weighted random pick (a Gumbel-max-style trick still needs `log`, so instead
  use a simple normalized-cumulative-sum table over the raw logits shifted into a non-negative
  range, which needs only integer add and compare), or (b) a small `exp`-approximation LUT (the
  same shape as the `tanh` table) applied to each logit before the same integer
  cumulative-sum sampling. Either avoids `expf` entirely, which is the same libm dependency the
  linked llama2.c analysis flags as a softfloat cost center on this core.

## 3. Concrete recommendation: a from-scratch, DiscoBSD-trained char predictor

**Architecture.** A char-level MLP: embedding `E=8`, context `k=3` (predict character `t` from
characters `t-3..t-1`), one hidden layer `H=64-128` with the LUT-`tanh` nonlinearity from
Section 2, and a `V=96`-wide output layer over printable ASCII plus newline, sampled with the
integer cumulative-sum scheme above. This is the `E=8, k=3, H=64` or `H=128` row from the sizing
table: **~8.9-16.6 KB of weights**, leaving room in the 25 KB budget for:

- **Code**: the forward pass is three small loops (embedding lookup, two matmuls, one LUT
  activation, one LUT-or-raw-logit sampling pass) -- comparable in scope to llama2.c's own
  ~700-line `run.c` minus its attention/RoPE/KV-cache machinery and minus every libm call, so a
  C implementation in the 150-300 line range, plausibly 2-4 KB of compiled Thumb-1 code.
- **LUT tables**: 512 B-1 KB for `tanh`, similar for the sampling table if a nonlinear one is
  used.

Total estimate: **~12-20 KB** for the `H=64` configuration and **~20-25 KB** for `H=128`,
comfortably inside the 25 KB target with margin at the smaller hidden size, and at the edge of it
at the larger one -- start at `H=64` and grow only if bpc on a held-out sample justifies the
extra 8 KB.

**A recurrent variant (optional stretch).** Feeding the hidden layer's own previous output back
in as part of next step's input (a minimal Elman-style char-RNN, architecturally identical to
Karpathy's `min-char-rnn.py` reference) removes the fixed context window `k` in favor of an
implicit unbounded one, at the same per-step weight cost as the MLP case above (the recurrent
weight matrix is `H x H` additional int8 weights: `64*64 = 4,096` bytes at `H=64`). This raises
quality (RNNs generally beat fixed-window MLPs at the same hidden size on char-level bpc) at a
known, small additional cost, and is a natural v2 once the MLP version is proven on-device. It is
not the initial recommendation because it adds a second matrix and a persistent hidden-state
buffer to get right on first bring-up, and the MLP alone already demonstrates every constrained
part of the problem (fixed-point matmul, LUT nonlinearity, LUT-free sampling).

**Training corpus and licensing.** Train from scratch, on the host, on **the DiscoBSD tree's own
text**: man pages, `/usr/share/doc`, source-code comments, and shell command names already
present in this repository. This is BSD-licensed (the tree's own license) by construction, so it
sidesteps the licensing question in Section 4 entirely rather than requiring evaluation of a
third-party corpus -- no need to argue about TinyStories' `cdla-sharing-1.0` license (a
share-alike license, not one of MIT/BSD/Apache/public-domain, and not appropriate to pull in
here per the licensing rule this document must follow) or any other external text's terms. As a
bonus, a predictor trained on the tree's own command names, flag spellings, and prose style is
the more genuinely useful artifact of the two "genuinely useful" options the task lists: a
next-word/next-character completion hint for shell commands and man-page prose on the device
itself, rather than a generic toy story generator trained on text with no relationship to what a
DiscoBSD user actually types. If a larger, more varied corpus is wanted for a better bpc number,
Project Gutenberg's out-of-copyright texts (US public domain) are the fallback that still
satisfies the licensing requirement -- TinyStories is explicitly not used as source text for this
project's own trained weights for that reason, even though it is a fine external reference point
for what "tiny" model sizes look like (Section 1).

**Host-side training and export.** Train in plain NumPy or a minimal PyTorch script (no
framework dependency needed at inference time, only at training time, on the host): read the
corpus, build the `(context -> next-char)` training pairs, train the float32 MLP with ordinary
backprop, then post-training-quantize each weight matrix to int8 with a per-layer symmetric
scale (round-to-nearest, clamp to [-127,127]), and emit the quantized tensors as C arrays
(`static const int8_t W1[...] = { ... };`) via a small Python export script -- the same shape as
`llama2.c`'s own `export.py`, but targeting a C header instead of a raw binary blob, since the
weight count here is small enough to compile directly into the binary rather than being read
from a separate file at runtime. This keeps the on-device program a single self-contained a.out
executable with no separate weights file to manage on the Dhara-backed filesystem.

**On-device inference code shape.**

```c
/* femtollm.c -- integer-only char predictor forward pass, no libm. */
#include "femtollm_weights.h"   /* generated: EMBED[96][8], W1[24][64], B1[64],
                                    W2[64][96], B2[96], TANH_LUT[512] */

#define VOCAB 96
#define CTX   3
#define EDIM  8
#define HDIM  64

static int8_t hidden[HDIM];

static void forward(const uint8_t ctx[CTX], int32_t logits[VOCAB]) {
    int32_t acc;
    int8_t  in[CTX * EDIM];
    for (int i = 0; i < CTX; i++)
        for (int j = 0; j < EDIM; j++)
            in[i * EDIM + j] = EMBED[ctx[i]][j];

    for (int h = 0; h < HDIM; h++) {
        acc = B1[h];
        for (int i = 0; i < CTX * EDIM; i++)
            acc += (int32_t)in[i] * W1[i][h];
        hidden[h] = tanh_lut(acc >> SCALE_SHIFT_1);   /* LUT, no float */
    }

    for (int v = 0; v < VOCAB; v++) {
        acc = B2[v];
        for (int h = 0; h < HDIM; h++)
            acc += (int32_t)hidden[h] * W2[h][v];
        logits[v] = acc;
    }
}
```

Sampling walks `logits[]` with the integer cumulative-sum scheme from Section 2 (no `exp`, no
`float`), and the top-level loop shifts a 3-character context window, calls `forward()`, samples
a character, prints it (or offers it as a completion candidate for the current shell line), and
slides the window. Build target: the tree's `arm-none-eabi-gcc` cross toolchain (the same one
`BOOT-MAP.md` lists as a host requirement) with `-mcpu=cortex-m0plus -mthumb -msoft-float`,
producing an a.out binary through the same `as`/`ld` path `distrib/rp2040/mkboardlibc.py`
already uses for the board's own libc -- no new toolchain component is needed, unlike the
Thumb-1 self-hosting assembler/compiler work `llama89-and-toolchain.md` scopes separately.

**Estimated size.** ~9-17 KB weights (`H=64`) or ~17-25 KB (`H=128`) plus ~2-4 KB compiled code
plus ~0.5-1 KB LUT tables: **roughly 12-22 KB total at `H=64`**, the recommended starting point,
with `H=128` as a documented stretch goal that should only be taken if a measured bpc improvement
on the tree's own corpus justifies the extra 8 KB against the 25 KB ceiling.

**Fallback if the MLP underperforms.** If on-device or host-simulated bpc from the quantized MLP
comes in worse than expected (quantization noise on an 8-bit hidden layer can matter more than
float training suggests), the hashed order-2/3 Markov table from Section 1 is the lower-risk
fallback: it needs no activation functions, no training loop convergence, and no quantization
step at all -- only integer counting over the same DiscoBSD-tree corpus, with its own table size
directly tunable to whatever remains of the 25 KB budget after the code (which is smaller than
the MLP's, since there is no matmul, LUT, or embedding lookup, only a hash and an array index).

## 4. Licensing

Per the requirement that only MIT/BSD/Apache/public-domain training code or checkpoints are used,
or text trained from scratch on permissively-licensed corpora:

- `llama2.c` (Karpathy): MIT. Confirmed by direct fetch of its `LICENSE` file. Referenced here
  for architecture and export-format ideas only -- no checkpoint or code from it is used in the
  recommendation above, since Section 1/2 already rule the transformer architecture out on size
  grounds.
- `femtoGPT` (keyvank): MIT. Referenced for background only, same reasoning as above.
- `llama4micro` (maxbbraun) and `PicoLlama` (earlephilhower): MIT. Referenced as embedded-port
  anchors only, not as a code or checkpoint source.
- `EmbedLlama` STM32 port (schuhandreas): license not confirmed by primary-source fetch in this
  search; referenced only for its published RAM/flash/tok-s numbers, not as a code source.
- **TinyStories dataset** (`roneneldan/TinyStories`): licensed `cdla-sharing-1.0` (Community Data
  License Agreement -- Sharing), a share-alike license, **not** one of MIT/BSD/Apache/
  public-domain. It is explicitly **not used** as training text for this project's own weights
  for that reason; it appears in this document only as a size/quality reference point for
  existing published models (Section 1).
- `karpathy/char-rnn`: license not confirmed by primary-source fetch of its own `LICENSE` file in
  this search (a search result attributed "BSD License" to an unrelated fork, not the origin
  repository). Do not import its code without checking that file directly; this document
  references only its publicly reproduced algorithm (the recurrent char-level forward/backward
  pass), not any specific repository's source text.
- TensorFlow Lite Micro: Apache 2.0. Referenced for its published footprint numbers only; ruled
  out in Section 1 on size grounds, not license grounds.
- **The recommended training corpus is the DiscoBSD tree itself** (this repository's own
  BSD-licensed man pages, docs, and source comments), which requires no external license
  evaluation at all. Project Gutenberg (US public domain) is named as a fallback corpus if a
  larger training set is later wanted.
- All training code for the recommended design (Section 3) is written from scratch for this
  project; no third-party training script is reused.

## Sources

- [karpathy/llama2.c](https://github.com/karpathy/llama2.c) -- MIT license (confirmed by direct
  `LICENSE` fetch), README architecture table, `export.py` Q8_0 quantization scheme
- [karpathy/llama2.c, doc/stories260K.md](https://github.com/karpathy/llama2.c/blob/master/doc/stories260K.md)
- [karpathy/tinyllamas, stories260K](https://huggingface.co/karpathy/tinyllamas/tree/main/stories260K) --
  `stories260K.bin` 1,056,540 bytes, `tok512.bin` 6,227 bytes
- [schuhandreas/embedllama-stm32h7a3](https://github.com/schuhandreas/embedllama-stm32h7a3) --
  STM32H7A3 (Cortex-M7, FPU) port; ~640 KB KV cache, ~132 KB tokenizer tables, 6.5-87 tok/s
- [hackaday.io/project/205074, "On-Chip LM: TinyStories 260K on Cortex-M7"](https://hackaday.io/project/205074/instructions)
- [earlephilhower/PicoLlama](https://github.com/earlephilhower/PicoLlama) -- RP2350 + PSRAM,
  MIT license, ~1.18 tok/s
- [maxbbraun/llama4micro](https://github.com/maxbbraun/llama4micro) -- Coral Dev Board Micro,
  MIT license, ~2.5 tok/s
- [karpathy/llm.c](https://github.com/karpathy/llm.c) -- MIT license, GPT-2-scale training in C
- [karpathy/nanoGPT](https://github.com/karpathy/nanoGPT) -- MIT license, PyTorch trainer
- [keyvank/femtoGPT](https://github.com/keyvank/femtoGPT) -- MIT license, pure Rust GPT
- [karpathy/char-rnn](https://github.com/karpathy/char-rnn) and
  [min-char-rnn.py gist](https://gist.github.com/karpathy/d4dee566867f8291f086) -- license of
  the origin repository not confirmed by primary-source fetch in this search
- [petewarden.com, "Launching TensorFlow Lite for Microcontrollers"](https://petewarden.com/2019/03/07/launching-tensorflow-lite-for-microcontrollers/) --
  ~16 KB interpreter core, "another 25KB of Flash" plus "30KB of RAM" for a wake-word model
- [tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro) -- Apache 2.0
- [Fenwick, "PPM compression without escapes"](https://www.cs.auckland.ac.nz/~peter-f/FTPfiles/PPMnoEsc.pdf) --
  ~2.2 bits/char, "a few hundred kilobytes" of context-model data space
- ["Prediction by Compression"](https://arxiv.org/pdf/1008.5078) -- PPM-as-predictor framing
- [Princeton COS126, "Markov Model of Natural Language"](https://www.cs.princeton.edu/courses/archive/spring11/cos126/assignments/markov.html) --
  order-k context-table design for char-level Markov prediction
- [huggingface.co/datasets/roneneldan/TinyStories](https://huggingface.co/datasets/roneneldan/TinyStories) --
  dataset license `cdla-sharing-1.0` (not MIT/BSD/Apache/public-domain; not used as training
  text here for that reason)
- `sys/arch/rp2040/doc/research/llama89-and-toolchain.md` (this tree) -- the transformer/KV-cache
  infeasibility analysis this document builds on, and the `arm-none-eabi-gcc` cross-toolchain
  and a.out build path this document's recommendation reuses
- `sys/arch/rp2040/doc/BOOT-MAP.md` (this tree) -- host toolchain requirement
  (`arm-none-eabi-gcc`, `bmake`, `picotool`)
- `distrib/rp2040/Makefile.inc`, `distrib/rp2040/mkboardlibc.py` (this tree) -- confirms the
  board's userland is built in a.out format throughout with the tree's own `as`/`ar`/`ld`
