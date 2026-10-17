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
| packed root data blocks | 693 | 692 | -1 |
| packed root reported free blocks | 286 | 287 | +1 |

The full build archive grows from 405960 to 410038 bytes because it gains four
independently selectable members. The packed root ships the unchanged 54086-byte
board libc subset. The full archive is therefore a build surface rather than a
constrained flash or process surface for this image.

The 988-block root uses nine inode-list metadata blocks in both images. The
comparable accounting identities are `693 + 286 + 9 = 988` for the base and
`692 + 287 + 9 = 988` for the candidate. The machine-readable companion
`libc-array-memory-line-reading.json` binds those counters and rejects
arithmetic drift.

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

The target disassembly reports a 24-byte local frame for both the old and new
`memmem()` implementations. The former private `getline()` contributes 48
bytes of local and callee-save stack; the promoted `getline()` wrapper and
`getdelim()` engine contribute 8 and 40 bytes along the same call chain. The
300-byte growth witness ends with a 512-byte caller-owned buffer. A target
`realloc()` that must move the second growth can temporarily hold the old
256-byte and new 512-byte payloads, so the bounded payload peak is 768 bytes,
excluding allocator metadata. The predecessor follows the same 128, 256 and
512-byte allocation sequence. These mechanism bounds do not establish the
whole-program runtime heap or stack peaks, which remain unmeasured.

## Behavioral and composition evidence

`bmake MACHINE=rp2040 check-libc-array-memory-line` unconditionally executes
the native line-reading witness and covers multiplication
overflow, allocator failure, preserved ownership, zero-sized requests, bounded
binary search, empty needles, delimiter retention, embedded NUL data, EOF,
stream errors, caller-owned buffers and refused growth. Native and ILP32
sanitizer variants execute separately. The line-reading variants report 44 and
46 checks respectively.

`bmake MACHINE=rp2040 check-libc-array-memory-line-cross` compiles the four
production sources with strict C17 Cortex-M0+ flags, rejects GNU `typeof`, checks
the archive member set, proves exact promoted-member extraction for empty and
per-interface consumers, and checks each affected consumer map. `getline()` is
the deliberate two-member case because it delegates to `getdelim()`. Calibrated
controls remove `memmem.o` from an archive, add an unexpected member to a
per-interface map, and inject a direct `LOAD memmem.o`; all controls must fail
for the gate to pass. The root cross graph runs the composition gate after the
parallel contract group because it cleans and relinks shared program outputs.

The clean distribution build and the focused host, cross and execution-inventory
gates passed. Renode and physical-board execution remain unrun. These results
promote the libc mechanisms and do not close the DPUCP Issue 8 requirement rows;
each row still needs a facet audit against its cited standard text before the
existing executable witnesses can support a row-closure decision.
