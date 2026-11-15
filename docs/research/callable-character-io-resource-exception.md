# Callable character-I/O resource exception

The complete callable-I/O candidate adds exactly 500 bytes to the installed
native libc archive. It adds `getchar.o` and `putchar.o` to that archive and
adds `getc`/`putc` to existing members. The other 161 of 162 shipped numeric
surfaces stay unchanged. All 34 shipped program records, including artifact
hashes, remain identical. The root retains 752 data blocks, 227 free blocks and
29 free inodes. Approval remains pending.

The [profile](dpucp-2024.md) requires exact deltas, examined alternatives,
remaining margins, user benefit, a rollback point and reviewer approval.
Earlier resource decisions retain their original scope.

## Source and artifact correspondence

The baseline is main `a706c0b851daaa25b019639e8217dda5ac17095d`.
The complete measured candidate is
`51dcf8e1b5995a64c5d586d6c04e25bb69697f78`.
The [parent](character-io-resources-parent.json),
[candidate](character-io-resources-candidate.json) and
[comparison](character-io-resources-comparison.json) retain identical collector,
measurement-tool and toolchain identities across clean distributions.
The candidate distribution completes at `2026-11-15T14:39:09Z` with exit zero.
The comparator rejects new archive members and positive growth under default
admission. That rejection supplies no exception approval.

| Shipped surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Native `/usr/lib/libc.a`, bytes | 50,690 | 51,190 | +500 |
| Selected native members | 172 | 174 | +2 |
| Root allocated data blocks | 752 | 752 | 0 |
| Root free blocks | 227 | 227 | 0 |
| Root free inodes | 29 | 29 | 0 |

The archive occupies 50 data blocks in both captures. Its last data block
contains 510 unused bytes in the parent and 10 in the candidate. The image
measurement establishes the unchanged allocation, including indirect blocks.
Kernel numeric resource vectors and native startup remain unchanged; kernel
artifact identities retain their independent build records. Native archive
membership changes only by the two named additions. Existing `fgetc.o` and
`fputc.o` change; the remaining existing native members retain their hashes.

The incomplete candidate `4b1b0fc1` has an archive-only 48-byte increase.
Its reduced archive lacks the two standard-stream providers and fails the new
archive gate. That vector stays outside this admission request.

## Native caller cost and alignment

Cross-linked shipped programs discard unused function sections. The native
a.out linker selects whole members and rounds final text to 16 bytes
(`usr.bin/ld/ld.c`, `middle`). Each new `getc`/`putc` wrapper occupies eight
Thumb bytes. A member shared with `fgetc` or `fputc` therefore introduces code
even when a native caller uses only the original entry. Separate executable
symbol records also affect raw file size and packing.

The [native receipt](character-io-native-cost.json) records three initialized
function-pointer probes. The [replay](character-io-native-cost.py) reconstructs
the two baseline members and requires exact matches to their retained SHA-256
identities. All other members come from the measured candidate. Removing the
two newly selected standard-stream members reconstructs the baseline selection.
Startup, compiler, native tools and candidate archive identities bind the run.
Native linker traces record the same selected members for each paired probe.
Changed baseline-member and archive identities each reject before a receipt
counts.

| Native probe | Loaded/text delta | Raw delta | Packed delta | Raw/packed block delta |
| --- | ---: | ---: | ---: | ---: |
| `fgetc` address | +16 | +28 | +9 | 0 / 0 |
| `fputc` address | +16 | +24 | +9 | 0 / 0 |
| Both addresses | +16 | +36 | +12 | 0 / 0 |

These link probes stay outside the shipped manifest and execute no native
program. Their packing deltas describe their exact sources and artifacts.
For an unchanged native selection containing one new eight-byte body, final
16-byte rounding admits zero or sixteen additional text bytes. Selecting both
adds sixteen text bytes. This arithmetic supplies no universal packed-size
bound for arbitrary programs.

ARM GCC `-fstack-usage` reports eight static local bytes for each of the six
entries. The new wrapper-to-provider chain contributes sixteen bytes before
the byte backend, while the existing `fgetc`/`fputc` frames remain eight.
Backend call chains, runtime stack/heap peaks, timings and swap traffic remain
unmeasured. The frame probe uses GNU17, Cortex-M0+, Thumb, soft float,
`-nostdinc -Iinclude -Os -fno-common -ffreestanding -D_TIME_BITS=32` with
`-fstack-usage`; `arm-none-eabi-nm -S` identifies the two eight-byte wrappers.

Replay from the repository root uses the caller-selected interpreter:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
"$PYTHON" docs/research/character-io-native-cost.py \
    --baseline docs/research/character-io-resources-parent.json \
    --candidate docs/research/character-io-resources-candidate.json \
    --output .build-logs/character-io-native-cost.json
```

## Benefit, alternatives and admission limit

The [contract](callable-character-io-contracts.md) supplies the public names
required when callers suppress macros or take function addresses. Portable
typed delegation reuses the existing byte mechanism. The reduced archive's
explicit two-member addition closes the standard-stream callable forms.
Omitting those members preserves the observed native link failure. Relying on
macro expansion leaves callable symbols unavailable. Compiler aliases and
combined source ownership introduce separate compiler/address/provenance
obligations; neither has qualified a lower complete cost here.

The requested exception admits exactly 500 additional native archive bytes,
the two named selected members, and the separate native-probe deltas above.
It admits no growth on the other 161 shipped numeric surfaces. The baseline
source supplies the rollback point. Additional growth requires its own
measurement and decision. This record carries no complete POSIX or physical
runtime acceptance claim.

The focused header, byte/backend, native/ILP32/sanitizer, Smaller C and Cortex-M0
QEMU witnesses pass. Both native archives pass with all 36 missing-member
controls rejected. Complete aggregate, required-inventory, Renode, hosted CI
and bounded external review retain their separate qualification states.
