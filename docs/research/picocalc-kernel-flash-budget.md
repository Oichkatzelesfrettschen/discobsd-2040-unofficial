# Full PicoCalc kernel size and SD root capacity

The full kernel fits 1,536 more bytes of linked code or data into its
original reservation with the same configured services. The SD-root
configuration adds another 32,768 bytes of reservation. Only the first
1,536 bytes count against the requested reduction to 95,880 bytes.
The full kernel still needs a further 31,232-byte reduction.

## What a byte buys

Measurements use base revision `bff303aae87864a9159bb310e0392b9c6316942c`,
GCC 16.2.0, ARM GNU ld 2.47.20260726 and Cortex-M0+ Thumb code. The
headroom checker subtracts `0x10000100` from the highest initialized
flash load end. This includes the gap before the 256-byte-aligned board
descriptor and the initializers copied to SRAM. The separate 256-byte
boot2, zero-fill storage and ELF debug records contribute zero to this
span. File sizes from UF2 include transport framing. The baseline flat
binary including boot2 has SHA-256
`51e216e906e9483734c34722f48f0a8b3ab97725f057283fdae3b4231b7a410a`.

| Configuration or experiment | Flash span, bytes | Capacity after boot2 | Free bytes before the 1,024-byte build margin |
|---|---:|---:|---:|
| Conventional full, baseline | 128,648 | 130,816 | 2,168 |
| Conventional full, compact panel programs | 128,136 | 130,816 | 2,680 |
| Conventional full, compact programs and shared data sections | 127,112 | 130,816 | 3,704 |
| Required SD root, original panel and section choices | 128,392 | 163,584 | 35,192 |
| Required SD root, compact panel programs | 128,136 | 163,584 | 35,448 |
| Required SD root, compact programs and shared data sections | 127,112 | 163,584 | 36,472 |
| Required SD root plus 32,768 linked test bytes | 159,880 | 163,584 | 3,704 |

For a fixed capacity `C` and measured span `S`, free space is `C - S`.
The composed gain over the original full kernel is exactly 34,304 bytes.
Subtracting the normal build margin leaves 35,448 bytes available for
growth in the SD profile and 2,680 in the conventional profile. The
1,536-byte shrink and the 32,768-byte partition change measure different
operations. Individual source changes can interact at descriptor alignment
boundaries. The table uses the actual combined links.

## C17 data layout and compiler experiments

1. **Store each varying part of a panel program once.** Each old row
   contains three pointers and three lengths. Its ARM size including
   padding is 20 bytes. The preamble has two choices and the base has
   two. Two byte selectors leave just one pointer and one 16-bit length
   for the suffix. For 28 programs the table drops from 560 to 224 bytes.
   Branches, literal pools, startup code and descriptor alignment determine
   the 512-byte conventional image delta. The `NONE` sequence emits a
   real NOP. A zero-length substitution changes bus traffic. The Unicorn
   gate compares register writes and requested delays for startup and all
   28 programs to the baseline, checks both invalid index boundaries and
   rejects a stub that reports success without issuing writes.
2. **Group data by the driver that owns it.** One ELF input section per
   variable lets the linker collect unused variables. It also prevents GCC
   from naming several nearby static positions through one base address.
   `lcdterm.c`, `lcdgfx.c` and `picokbd_evq.c` have cohesive private state.
   Turning off per-variable sections on those three units decreases their
   initialized, allocated object payloads by 640, 232 and 160 bytes.
   Cortex-M0+ GCC enables section anchors at `-Os`. The full image spans
   after each successive substitution are 127,624, 127,368 and 127,112.
   `lcdterm_putc` drops from 2,312 to 1,988 bytes. The ordinary SRAM end
   stays at `0x2003b5a8`. Individual functions retain their own sections,
   and the timing-counted PSRAM translation unit keeps its compile rule.
   The three units use the kernel's GNU17 dialect with fatal prototype
   and old-style-definition diagnostics. The PicoCalc host fixtures also
   exercise terminal and graphics behavior under their strict C17 profile.
3. **Measure compiler choices at the final link.** A sweep recompiles 92
   size-optimized translation units from the compact-panel configuration.
   It retains the other objects and the production GNU linker. GCC `-Oz`
   yields 128,136 bytes. Clang 23.1.4 `--target=arm-none-eabi -Oz` yields
   130,136 and fails the 1,024-byte margin. Choosing the smallest local
   initialized payload among production, GCC and Clang per unit yields
   125,784, of which 35 objects take a different compilation. This is a
   research link. A local minimum has no general guarantee of minimizing
   the linked span. The hot-layout gate accepts this image. Mixed-compiler
   ABI, stack, flash-off call closure, peripheral timing and performance
   remain to be qualified. A machine-independent LTO trial produces
   127,088 bytes. A related LTO trial places an interrupt helper and a
   capacity-counter clone outside the prescribed hot span. The existing
   flag sweeps also record a repeatable pipe throughput loss for a small
   size win on a bare Pico. Those outcomes keep blanket flag changes out
   of the production rule. [Prior sweeps](kernel-size-flags/README.md)

The per-object table records byte counts before final section collection.
The GCC anchor column uses `-Os -fno-data-sections -fsection-anchors`.
The GNU `-Oz` and Clang columns retain per-variable sections. Each sum
counts allocated PROGBITS from the relocatable object, including code,
literals and initialized data. Map padding, cross-object string merging
and dead sections decide the final cost. In the baseline map, the linker
attributes 6,286 bytes of merged strings to an input from `exec_conf.o`.
Crediting that entire pool to executable-format dispatch gives a false
estimate of removable code. [Per-object measurements](kernel-size-flags/picocalc-objects.tsv)

## Resident kernel, transient code and storage

The physical flash change transfers four 8 KiB Dhara erase units to the
kernel and preserves the raw swap start at `0x101a0000`. The independent
card filesystem supplies root and installed programs. A paired build
contains 988 filesystem blocks with 66 free and seven free inodes on the
conventional flash root. The 32,768-block card image has 16,751 free blocks
and 2,350 free inodes. These counts describe the recorded installation.
A shift of the root address changes Dhara page numbering. The old journal
cannot serve as the image for the shifted layout.

Boot2, exception and interrupt paths, SD transport, root mount, exec,
scheduling and swap remain resident. A removable medium can fail or sleep
during an I/O call. Code that must complete that same call, restore XIP
or deliver a fatal diagnostic needs an independent execution path. The
first type-b7 partition becomes root and pipe device. An ordinary SD
program acquires the existing user window through exec, with the BSD
syscall ABI, credentials, signal handling and exit cleanup. `cal 2 2000`
in the emulator exercises a program present only in the extended manifest
and prints the leap day. The existing user-mode v7fs utility supplies a
larger example of a filesystem component with file-descriptor I/O.
There is no privileged module ABI in either profile. [Component measurements](tiny-rump-components.md)

The test link forces one read-only section of 32,768 bytes live with GNU
ld's `--undefined` and uses the production objects and linker scripts.
The expanded ELF places its board descriptor at `0x10026d00`, its data
load at `0x10026d10` and its RAM routine initializers at `0x10026da0`.
Boot now depends on bytes beyond the conventional limit. Under the old
map this ELF exceeds capacity by 29,064 bytes even with zero margin.
The normal UF2 builder, board descriptor tool and emulator run against
the expanded map. The Renode cases cover card login and mount, file
round-trip through the separate temporary partition, a missing root,
and foreign flash samples at seven addresses. Renode supplies the panel,
keyboard and SD models. A physical loader, real media removal, NOR erase
timing and display latency require separate observations.

## Limits from the actual binary

The baseline `.text` output holds 127,234 bytes and includes instructions
and read-only data. ELF symbol sizes attribute 112,474 bytes to function
symbols in the kernel flash range. The non-hot read-only tail of the
output section occupies 13,538 bytes (0x1001bd20 through 0x1001f202,
exclusive). Even removing every byte of this tail leaves 19,230 of the
original 32,768 bytes to find elsewhere, before replacement code. Symbol aliases and anonymous literals
limit that attribution. The 12,888-byte gap below the first u-area includes
512 bytes reserved for the dump probe. It leaves 12,376 ordinary SRAM bytes
for additional owners under this layout.

A byte-for-byte packing trial slices the baseline flat binary after boot2
at fixed offsets, compresses each slice independently and retains the
shorter of raw and encoded bytes. A single zlib level-9 stream takes
94,699 bytes. Decompressing the full stream takes an output destination
for 128,648 bytes. Independent 4 KiB slices take 99,363 bytes with zlib
and 99,524 with Python's default XZ/LZMA. Independent 8 KiB slices take
97,461 and 94,892 respectively. The last figure leaves just 988 bytes
under the target before a directory, decoder, resident interrupt code,
workspace or call transitions. Two simultaneously pinned 8 KiB pages
alone exceed the ordinary SRAM budget. These trials measure storage
density. Calling a routine across page boundaries also requires branch
relocation, literal identity, stable indirect targets and safe returns
after an interrupt or a sleeping kernel call.

Coreboot 4.8 at `ebdeb4d0`, `src/lib/cbfs.c`, records both the compressed
stage length and its destination capacity. Its stage loader decompresses
to that destination before transfer of control. That destination is the
missing resource in a whole-kernel SRAM unpack on this RP2040 layout.
The gororoba-open tree at `f63fb5b2` provides a useful negative control.
Its `crates/algebra_experimental/src/novel_algorithms/void_compression.rs`
classifies a 16-component floating-point block by a small-norm threshold.
Distinct blocks below the threshold yield the same flag. Kernel opcodes,
addresses and MMIO table entries need exact reconstruction. The baseline
panel sequences share exactly selectable byte strings, which supplies a
lossless equivalence relation and a direct execution oracle.

One aligned 256 KiB MPU region with its upper two eighths disabled covers
the 192 KiB user window arithmetically. It would free a permission slot
and zero bytes of flash reservation. The existing map has spare slots.
Changing protection requires the startup register readback and fault
evidence described in the MPU guide. [MPU map](../../sys/arch/rp2040/doc/MPU.md)

## Remaining 31,232 bytes and acceptance criteria

| Step | Mechanism and deliverable | Decisive measurement or rejection |
|---|---|---|
| Attribute the resident code | Use the final map and relocation inputs to assign native code, shared strings, literal pools, alignment, RAM initializers and indirect roots. Preserve separate rows for vectors, panic, flash-off routines and core 1. | Reconcile the sum to the ELF span. Charge every shared byte once. Reject estimates that add whole object-file sizes. |
| Compress native state access | Apply the driver experiment to further cohesive state groups. Preserve explicit lifetimes, volatile access, assembly offsets and public structures. Test each translation unit at the same revision. | Smallest positive link delta, stable ABI and stack, and the existing owner-specific negative controls. Rebuild the complete image after each accepted combination. |
| Evaluate larger code owners | Profile tty input and ioctls, name lookup, exec and swap. Factor the repeated state transitions only where error return, sleep points, interrupt level and ownership agree. | Compare unchanged traces and fault paths, frame bounds, sizes and workload timings. Removing an entry point or shrinking a live table fails this lane. |
| Move a genuinely cold service | Prefer an existing user-mode executable and file-descriptor backend. Keep the resident syscall and recovery path bounded. Measure the permanent kernel entry, user image and largest simultaneous resident working set. | Exercise invocation, bad requests, signals, unavailable card and recovery. Charge the new resident ABI before claiming any additional saving. |
| Consider native overlays only for a closed call graph | Enumerate direct calls, function pointers, return edges, exception targets and blocking sections. Budget input, decompressor, two live return paths and the worst interrupt before selecting a slot. | Require bounded live pages below the SRAM limit, stable unwind and debugger behavior, and complete short-read or media-loss recovery. Decline a privileged loader without a trusted source and an enforceable contract. |
| Qualify either architecture | Use an actual PicoCalc to measure repeated shell, pipe, console, graphics and filesystem operations, plus the configured loader, card and recovery combinations. | Keep latency distributions and SRAM peaks separate from linked byte counts. Fixed-layout success requires an independently bootable full image at or below 95,880 bytes with all features. |

The flash layout, profile selection and build commands live in the
[profile guide](../../sys/arch/rp2040/doc/PROFILES.md). The linked-image
checks, register-trace replay and SD boot conditions live in the
[test guide](../../sys/arch/rp2040/doc/TESTING.md).
