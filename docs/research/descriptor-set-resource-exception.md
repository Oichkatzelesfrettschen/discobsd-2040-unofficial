# Descriptor-set initialization resource exception

## Candidate and exact limits

Candidate `15113728a000ede7d57eeb829518445a8c341d27` compares with owned main
`a706c0b851daaa25b019639e8217dda5ac17095d`. Independent clean distributions
bind the same toolchain and measurement tools, captured compiler/linker inputs,
source snapshots, selected archive members, and actual selected root manifests.
The baseline completes before the production edits. The candidate capture
starts from a clean tracked tree at its named commit.

The [parent](descriptor-set-resource-parent.json),
[candidate](descriptor-set-resource-candidate.json), and
[paired comparison](descriptor-set-resource-comparison.json) retain all 162
shipped resource measures. The single positive shipped delta is three packed
bytes for gamebox. Its loaded and raw sizes decrease by four bytes. Getty
also decreases. The other 156 measures stay unchanged, including both kernels,
installed native libc/startup, BSS, every allocation-block count, and the root.
Every archive selection and selected member identity stays unchanged.

| Selected program | Loaded delta | Raw delta | Packed before | Packed after | Packed delta | Packed allocation blocks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `/usr/games/gamebox` | -4 | -4 | 21,240 | 21,243 | +3 | 22, unchanged |
| `/usr/libexec/getty` | -4 | -4 | 13,237 | 13,230 | -7 | 14, unchanged |

The root retains 752 data blocks, 9 inode-list blocks, and 227 free blocks in
its 988-block logical partition. These values describe this image. They do
not enlarge any fixed hardware region or establish runtime headroom.

## Optional standalone forms

The [built-program receipt](descriptor-set-built-resources.json) compares all
278 ordinary a.out outputs retained by the two captured builds. Outputs match
by producer path; each retained artifact matches its captured SHA-256. A path
with conflicting producer bytes rejects. The calibrated hsaout tool measures
both sides; the existing a.out parser supplies text/data/BSS extents.

| Unshipped program | Loaded before | Loaded after | Raw before | Raw after | Packed before | Packed after | Packed delta | Packed allocation blocks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `games/coil/coil` | 11,084 | 11,084 | 6,208 | 6,208 | 5,771 | 5,773 | +2 | 7, unchanged |
| `games/fourfall/fourfall` | 14,548 | 14,544 | 7,088 | 7,084 | 6,447 | 6,451 | +4 | 8, unchanged |

Their raw allocation counts also stay unchanged. Standalone ridge, calc,
virus, and the puzzle consumers retain their measured sizes. The gamebox row
is the selected multicall form; these optional standalone forms contribute
no additional bytes to this root image.

Only gamebox, getty, coil, and fourfall have changed measured sizes among
those 278 outputs. Pdc has six changed timestamp bytes from the `__TIME__`
expansion in `usr.bin/pdc/pdc.y:810`, with identical sizes and blocks. The
retained byte comparison isolates that expected build-time identity change.
Source lookup of `FD_ZERO` and `gameterm.h` supplies caller leads; the complete
captured-output comparison supplies the measured size denominator.

## Mechanism, alternatives, and decision

The standalone clearing macro requires no application-provided memory-routine
declaration. Getty initializes its singleton readiness set. The implementation
preserves the fixed one-word ABI and all admitted descriptor behavior. The
[mechanism record](descriptor-set-initialization.md) owns source, normative
requirements, donor comparison, tests, and remaining semantic leaves.

A shorter instruction image can have a longer compressed representation.
The paired gamebox and fourfall rows demonstrate that nonmonotonic relation
under this compressor. ELF symbol inspection separately finds four fewer
bytes in fourfall's main function. Packed-byte savings in getty do not cancel
positive growth in another constrained surface.

Including `<string.h>` through the pervasive select header admits its existing
memory-routine call but expands public header dependencies. A compiler builtin
requires a separate Smaller C mechanism. The typed word loop supplies one
portable user implementation. Omitting the caller clear retains invalid
readiness state and fails its calibrated runtime witness.

Approval remains pending for exactly gamebox +3 packed bytes and optional
coil +2 and fourfall +4 packed bytes, with no positive loaded/raw, BSS,
allocation-block, native-archive, kernel, or root delta. The compatibility
profile requires this owner decision before integration. Existing byte-search
approvals retain their original scopes. Any additional positive delta requires
another measured decision.

## Validation and replay

Focused cross, host, ILP32 host, and Cortex-M0 qemu-user contracts pass. The
cross matrix contains 240 valid compiler cases and six required rejections;
80 Smaller C inputs compile and assemble. Each runtime good case covers 2,304
readiness/read/byte combinations, and four defect controls reject. Existing
getty contracts, source accounting, donor/profile controls, repository lint,
Clang analyzer, build, and revision text/comment checks pass.

The complete cross tier passes in 213.4 seconds. Its sandbox blocks optional
32-bit compiler-probe execution: the same harmless binary exits 159 inside
and zero outside, with SHA-256
`014a943fd57557b54c4aa6b5322d0762fcb34b1c07d24b0a048fb18cf2d1ee05`.
The separate clean aggregate passes in 721.58 seconds, and the required ILP32
inventory passes 216/216 in 563.1 seconds outside that restriction. Hosted CI
43709817864 passes. UART Renode passes in 447.6 seconds and union-kernel
Renode passes in 304.1 seconds. The [qualification receipt](descriptor-set-qualification.json)
binds the source and exact artifacts. It preserves the original inventory's
215 archived detailed receipts and the separately replayed final negative
recipe rather than attributing the follow-up to the original invocation.
Physical-board execution stays unrun. Runtime peak allocations and stack are
separate from the static measures.

With caller-selected `PYTHON`, replay the paired static check:

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
"$PYTHON" tools/c17/shipped_resources.py compare \
    --base docs/research/descriptor-set-resource-parent.json \
    --candidate docs/research/descriptor-set-resource-candidate.json \
    --output .build-logs/descriptor-set-resource-replay.json
```

The generic comparator writes the complete vector and rejects its positive
packed-byte delta. That rejection is the intended default policy. The exact
owner decision supplies the exception; changing the comparator to ignore it
supplies no resource approval.
