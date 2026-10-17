# Libc array, memory-search and line-reading promotion

The DPUCP-2024 foundational-libc change promotes `reallocarray()`, `memmem()`,
`getdelim()` and `getline()` as isolated archive members. The implementation
uses C17 control flow. `getline()` delegates to one shared `getdelim()` engine.
Standalone utilities select the archive members that they use, while textbox
selects each shared member once.

The measurement compares the clean base commit
`6a423112025f7f2766923f4ddc05f79f9bb097c0` with the Task 4 worktree after a
clean `distribution` build. The caller selected and exported
`PYTHON=/usr/local/bin/python3`. The measured tools were Python 3.14.7, bmake
20260912, arm-none-eabi-gcc 16.2.0 and GNU ld 2.47.20260726. The build command
was:

```sh
bmake MACHINE=rp2040 distribution
```

The strict Cortex-M0+ objects contain the following text bytes and zero data or
BSS bytes:

| archive member | text bytes |
| --- | ---: |
| `reallocarray.o` | 72 |
| `memmem.o` | 74 |
| `getdelim.o` | 200 |
| `getline.o` | 12 |

The new `getdelim.o` and `getline.o` pair totals 212 bytes, equal to the former
textbox-private `getline.o`. Archive isolation keeps each unused member out of
unrelated programs.

## Constrained surfaces

| surface | base | candidate | delta |
| --- | ---: | ---: | ---: |
| PICO kernel text | 104646 | 104646 | 0 |
| PICO kernel data | 248 | 248 | 0 |
| PICO kernel BSS | 39848 | 39848 | 0 |
| board libc archive bytes | 54086 | 54086 | 0 |
| largest process, `warp`, loaded bytes | 101300 | 101300 | 0 |
| packed root reported blocks | 702 | 692 | -10 |
| packed root reported free blocks | 286 | 287 | +1 |

The full build archive grows from 405960 to 410038 bytes because it gains four
independently selectable members. The packed root ships the unchanged 54086-byte
board libc subset. The full archive is therefore a build surface rather than a
constrained flash or process surface for this image.

| program | base loaded bytes | candidate loaded bytes | delta | base packed bytes | candidate packed bytes | packed blocks delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `cut` | 10784 | 10768 | -16 | 9091 | 9089 | 0 |
| `paste` | 9060 | 9044 | -16 | 7528 | 7502 | 0 |
| `expand` | 9384 | 9368 | -16 | 8119 | 8086 | 0 |
| `unexpand` | 9472 | 9456 | -16 | 8165 | 8139 | 0 |
| `nl` | 11140 | 11140 | 0 | 9263 | 9276 | 0 |
| `uudecode` | 11572 | 11572 | 0 | 9633 | 9657 | 0 |
| `textbox` | 38384 | 37684 | -700 | 30233 | 29532 | -1 |

`tools/bin/rp2040/size` supplies loaded text, data and BSS totals.
`tools/bin/rp2040/hsaout -s` supplies raw and packed executable sizes and block
counts. `tools/bin/rp2040/fsutil --check --partition=1` reports 95 files, 692
blocks and 287 free blocks for the candidate image.

Kilo was tested as an additional `getline()` consumer. The migration increased
Kilo's loaded image by 92 bytes, so the change restored Kilo's local reader.
The restored program measures 16108 loaded bytes, 15868 raw bytes, 13897 packed
bytes and 15 packed blocks, exactly matching the base. A separate ABI-safe
change may reconsider Kilo only when it supplies an offset on every constrained
surface.

## Behavioral and composition evidence

`bmake MACHINE=rp2040 check-libc-array-memory-line` covers multiplication
overflow, allocator failure, preserved ownership, zero-sized requests, bounded
binary search, empty needles, delimiter retention, embedded NUL data, EOF,
stream errors, caller-owned buffers and refused growth. Native and ILP32
sanitizer variants execute separately. The line-reading variants report 44 and
46 checks respectively.

`bmake MACHINE=rp2040 check-libc-array-memory-line-cross` compiles the four
production sources with strict C17 Cortex-M0+ flags, rejects GNU `typeof`, checks
the archive member set, and checks each affected consumer map. Calibrated
controls remove `memmem.o` from an archive and inject a direct `LOAD memmem.o`;
both controls must fail for the gate to pass.

The clean distribution build and the focused host, cross and execution-inventory
gates passed. Renode and physical-board execution remain unrun. These results
promote the libc mechanisms and do not close the DPUCP Issue 8 requirement rows;
each row still needs a facet audit against its cited standard text before the
existing executable witnesses can support a row-closure decision.
