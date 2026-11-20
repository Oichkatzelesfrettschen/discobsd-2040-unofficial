# Full PicoCalc kernel size and SD root capacity

The full kernel fits 1,792 more bytes of linked code or data into its
original reservation with the same configured services. The SD-root
configuration adds another 32,768 bytes of reservation. Only the first
1,792 bytes count against the requested reduction to 95,880 bytes.
The full kernel still needs a further 30,976-byte reduction.

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
| Conventional full, all six shared-state units and single console tty | 126,856 | 130,816 | 3,960 |
| Required SD root, original panel and section choices | 128,392 | 163,584 | 35,192 |
| Required SD root, compact panel programs | 128,136 | 163,584 | 35,448 |
| Required SD root, compact programs and shared data sections | 127,112 | 163,584 | 36,472 |
| Required SD root, all six shared-state units and single console tty | 126,856 | 163,584 | 36,728 |
| Required SD root plus 32,768 linked test bytes | 159,624 | 163,584 | 3,960 |

For a fixed capacity `C` and measured span `S`, free space is `C - S`.
The composed gain over the original full kernel is exactly 34,560 bytes.
Subtracting the normal build margin leaves 35,704 bytes available for
growth in the SD profile and 2,936 in the conventional profile. The
1,792-byte shrink and the 32,768-byte partition change measure different
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
3. **Use the console open contract to avoid a stack cost.** The keyboard
   controller, console driver and bell also share data within each unit.
   A trial link reduces the main text section by 152, another 46, then
   another 24 bytes. Together the substitutions cross a descriptor
   alignment boundary and return 256 bytes of flash. GCC's stack-usage
   output shows an eight-byte frame increase in `lcdwrite`. The driver
   admits one terminal minor and dispatches the graphics minor separately.
   Addressing the one tty directly removes the unused device-index
   multiplication. A C17 assertion ties the representation to the admitted
   minor range. The linked `lcdwrite` frame returns to eight bytes. The
   total main text reduction from these three units and the tty change
   is 254 bytes. The final flash saving stays at 256 bytes.

   Comparing the two full images with `stack_chain.py --from-table
   syscall_handlers` gives equal model depths for all 122 roots. Before
   the tty correction, read, readv, write and writev each gained eight
   bytes on its modeled maximum path. Two keyboard frames still grow by
   eight bytes and two shrink by eight. The model gives 3,548 bytes for
   the conservative combined u-area path against a 2,048-byte stack and
   1,512 against 2,048 for rendering in both builds. Those sums and their
   nesting assumptions are documented in the [stack study](kernel-size-flags/README.md).

4. **Measure compiler choices at the final link.** A sweep recompiles 92
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
   The open acceptance for LTO code while XIP is suspended is in
   [xip-off-linked-code-proof.md](xip-off-linked-code-proof.md).

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
and 2,350 free inodes. These counts describe the recorded installation;
the open floor after sustained use is in
[picocalc-writable-reserve.md](picocalc-writable-reserve.md).
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
The six-unit growth ELF places its board descriptor at `0x10026c00`, its
data load at `0x10026c10` and its RAM routine initializers at `0x10026ca0`.
The reset path reads all three beyond the conventional limit. The conventional
headroom checker rejects this ELF by 28,808 bytes even with zero margin.
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

The baseline ELF contains 866 distinct sized function intervals in the
XIP text section. Their union occupies 112,414 bytes. A disassembly
scan finds 215 intervals with decoded instructions and no apparent
`bl`, `blx` or `svc`. Their union holds only 9,598 bytes. Taking those
bytes out of the image entirely still falls 23,170 bytes short of the
original target. This is an optimistic filter for call-free code. It also
admits tail branches, functions reached by interrupts and functions that
access MMIO. The scan uses `arm-none-eabi-objdump -d -j .text` over the
nonempty STT_FUNC intervals from the ELF symbol table, clears the Thumb
bit on entry addresses and merges overlapping intervals before summing.
A larger decomposition has to handle calls across the residency boundary.

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

A full-kernel experiment on the three-unit build replaces the two SRAM
entries with one 256 KiB entry at `0x20000000`, with RASR `0x0300c023`.
Its table loses eight bytes. The board alignment absorbs the difference
and the flash span remains 127,112 bytes. Renode boots to a shell with
three programmed entries and control readback 5. The shipped `mputest`
reports one failure, its explicit expectation of four programmed entries.
Its user-window edge, aliases, kernel addresses, divider, ROM and caught
fault probes agree with their expected behavior in this run. The standard
firmware retains the board-qualified map. A physical readback and boundary
fault run decide whether the extra permission slot is usable on target.

## Which bytes must change representation?

The ARM ELF mapping symbols identify ranges of Thumb code (`$t`) and
embedded data (`$d`) within the main text section. Each interval runs to
the next mapping symbol or section end and includes any intervening fill.
The initialized load segments in `arm-none-eabi-readelf -lW` account for
the rest. Both columns exclude boot2.

| Owner of flash bytes | Original full | Compact full |
|---|---:|---:|
| Thumb ranges including adjacent fill | 101,692 | 101,256 |
| Data ranges inside the main text section, including vectors, literal pools and tables | 25,542 | 24,406 |
| Alignment before the board descriptor | 254 | 34 |
| Board descriptor | 16 | 16 |
| Initial data copied to ordinary SRAM | 144 | 144 |
| Instructions and literals copied to ordinary SRAM | 1,000 | 1,000 |
| **Linked span** | **128,648** | **126,856** |

The compact image's Thumb ranges alone exceed the 95,880-byte target by
5,376 bytes. An optimization that changes only the other rows therefore
cannot reach the target, even with zero storage for vectors, literals
and initialized RAM. The largest measured change in the two columns
comes from data embedded among instructions. The 220-byte decrease in
descriptor alignment amplifies the combined 1,572-byte change inside
the main text section.

**Sharing exact instruction sequences.** The compact image contains 68,322
bytes in runs of decoded instructions after splitting at a discontinuous
address, a branch, a supervisor call, an address calculation using the
program counter, or an explicit program-counter operand. The input is
`arm-none-eabi-objdump -d -j .text`. Sequence equality compares the
encoded halfwords, including both halves of a wide instruction.
For each run, count every contiguous sequence across all runs. At each
instruction start, the maximum saving is the larger of skipping that
instruction and replacing a repeated sequence of length `L` bytes at
cost `C` plus the best saving after it. Evaluate backwards with zero
saving at the end. The calculation prevents two substitutions from
claiming the same site. It grants free helper bodies, infinite branch
reach and replacement of every copy, including the first.

| Bytes per substitution | 2 | 4 | 6 |
|---|---:|---:|---:|
| Original full, optimistic saving | 14,122 | 4,854 | 2,338 |
| Compact full, optimistic saving | 13,912 | 4,806 | 2,300 |

A Thumb long call occupies four bytes. Even the two-byte model, which
charges only a short branch, leaves 17,064 bytes of the compact image's
30,976-byte deficit. Distinct register assignments, common control-flow
graphs and shared operations at the C level lie outside this byte-exact
model. A C rewrite can exploit those relationships when the error paths,
live registers and interrupt state agree.

**Pooling addresses.** A GNU ld relink of the compact image with
`--emit-relocs` records 2,080 absolute word relocations in `.rel.text`.
The four bytes at every such offset match the production link. Those
sites contain 910 distinct words. Keeping one of each saves at most
`4 * (2080 - 910) = 4,680` bytes before references, indexing code and
literal reach. The base of the current process's kernel area alone
appears 217 times. Sharing that address everywhere would save 864 bytes
of literal words before paying for access. GNU ld can exchange branch stubs on a relink. The word comparison covers
the measured sites in the release ELF.

**Compiling C to a portable instruction stream.** A host experiment
recompiles 97 size-optimized C units from the three-unit build with the PICOCALC defines and
include paths. Clang 23.1.4 targets `wasm32-unknown-unknown` at `-Oz`,
with GNU17, freestanding compilation, unsigned plain char, short enums,
fatal diagnostics, and the kernel's aliasing and builtin restrictions.
Each unit links independently with LLD 23.1.4 using `--no-entry
--export-all --allow-undefined --compress-relocations --strip-all`.
Binaryen 130 applies `wasm-opt -Oz --all-features`.

Of the 97 units, 30 contain target assembly or register operands that
the WebAssembly backend rejects. For the 67 that link, the production
ARM objects contain 60,456 bytes of `.text*` input sections, including
their literal pools. The optimized WebAssembly CODE sections total
88,014 bytes, before data sections, imports, a native interpreter and
the transitions between execution environments. Every linked unit costs
at least as much in this comparison. Per-unit export roots and the
linker's small synthetic functions affect the comparison. This is a
storage experiment on C translation units, with no runtime or ABI
qualification. It rules out a size gain from this specific general
bytecode pipeline. A small instruction language specialized for a
repeated device operation has a different cost, as the panel register
programs demonstrate. [Per-unit results](kernel-size-flags/picocalc-bytecode.tsv)

### The cost of keeping an interrupt path callable

A page experiment starts from the compact six-unit full kernel. It keeps the
6,720 bytes through `__hot_end` and all 1,194 bytes after the main text
section in place. That leaves 118,942 bytes to encode. The ARM Thumb branch
filter first normalizes branch addresses. Each page then uses either its raw
bytes or an independent LZMA1 stream, whichever uses less flash. Each
compressed page decodes byte-for-byte on the host. A second run also keeps
the three sized function intervals for `USBCTRL_IRQ_Handler`, `usbintr` and
`usb_service` in flash. Their direct call chain appears in the linked
disassembly. Its callees, data and other interrupt paths make the actual
resident set larger.

| Output bytes per page | Packed span with just the hot prefix and tail | Packed span also keeping three USB functions | USB case plus native decoder | SRAM left after one output page and probability table |
|---:|---:|---:|---:|---:|
| 4,096 | 95,836 | 96,118 | 98,434 | 3,052 |
| 6,144 | 93,891 | 94,410 | 96,726 | 1,004 |
| 7,040 | 93,337 | 93,828 | 96,144 | 108 |

The finite model uses the flat binary after boot2, SHA-256
`d841f49c9cf67bc53420aa1d93a0680d566970c074e77a0fceacb4630421633f`.
Split each movable interval from its beginning at the stated page length.
Restart at the end of each pinned function. Python's `lzma.FORMAT_RAW`
uses `FILTER_ARMTHUMB` with the page's offset from `0x10000100`, then
`FILTER_LZMA1` with preset 9, dictionary size 8,192, `lc=lp=0` and
`pb=2`. Count the pinned bytes once and add each shorter representation.

Coreboot 4.8 at `ebdeb4d0`, `src/lib/lzmadecode.h`, allocates
`2 * (1846 + (768 << (lc + lp)))` probability bytes. Zero literal context
bits reduce this to 5,228 bytes. The output page provides the match history.
The kernel has 12,376 available ordinary SRAM bytes after its dump reserve.
The SRAM column subtracts both owners from that budget. GCC 16.2.0
cross-compiles the donor's unmodified `src/lib/lzmadecode.c` with
`-std=c17 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ffreestanding
-fno-strict-aliasing -fno-builtin -ffunction-sections -fstack-usage`.
The `.text.LzmaDecode` section occupies 2,316 bytes and its reported frame
uses 104 bytes on the caller's stack. The size calculation grants fixed
decoder properties for free.

At 7,040 bytes per page, pinning just those USB routines changes an apparent
227-byte flash surplus into a 264-byte deficit. The directory, inverse branch
filter, transfer into SRAM, relocations and return path all still need
space. One sleeping kernel call can retain a return address into a page that
another call displaces. A replacement design has to preserve that address
or reload its target before returning. These results reject the three
measured page layouts with this decoder and resident set. They leave the
cost of other instruction formats and other page boundaries open.

## Reproduce the independent measurements

Run the commands in a dedicated checkout. The conventional link and the
SD-only system can be built independently. For the SD-only dependency
test, start at the `picocalc-sd-system` command in a fresh checkout. It
produces both outputs with no `compile/PICOCALC/unix.elf` or
`distrib/rp2040/sdcard-PICOCALC.img` present. The filesystem checker
reports 1,746 files in that fresh SD image.

```sh
PYTHON=${PYTHON:-python3}
export PYTHON
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-panel-programs check-kernel-headroom check-hot-layout
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 picocalc-sd-system
bmake MACHINE=rp2040 check-renode-picocalc-sd-system
tools/bin/rp2040/fsutil --check distrib/rp2040/sdroot-PICOCALC.img
arm-none-eabi-readelf -lW sys/arch/rp2040/compile/PICOCALC_SD/unix.elf
```

The fixed-layout success check uses the same verifier with 34,936 bytes
of required headroom (`130816 - 95880`). On the six-unit image this command
returns status 1 with 3,960 bytes of headroom.

```sh
"${PYTHON}" tools/check_kernel_headroom.py --margin 34936 \
    sys/arch/rp2040/compile/PICOCALC/unix.elf
```

To reproduce the growth test, build the optional kernel and UFS image
first. Assemble a read-only section containing `.fill 32768,1,0x5a`
under a global symbol. Take the build directory's link arguments from
`bmake -v LDFLAGS`, `bmake -v SYSTEM_OBJ` and `bmake -v LDADD`. Relink
the complete object list, `vers.o`, `swapunix.o` and the extra object in
that directory with `--undefined` naming the symbol and a separate
output ELF and map. Run the host `kernsum` on the ELF, then
`picotool uf2 convert` and `distrib/rp2040/mkboarduf2.py --board=PICOCALC`
on that result. These commands operate on files. Pass the stamped board
descriptor UF2 to `tools/renode/check-picocalc-sdroot.py --configured`
with the Renode executable, SD filesystem image and a private output
directory. The vector table stays free of the PicoCalc loader stamp for
this direct-boot test.

## Remaining 30,976 bytes and acceptance criteria

Let `B` be the linked bytes a proposed rewrite actually owns, `r` the
fraction it removes, and `H` the added resident code and metadata. A
first size screen is `r * B - H >= 30,976`. With zero overhead, a
rewrite that halves its input needs at least 61,952 input bytes. A
one-third reduction needs 92,928. A one-quarter reduction needs
123,904. A 16 KiB service, even if it disappears entirely, supplies at
most 16,384 bytes before its replacement entry point and alignment.
Choose the service boundary and measure its exclusive linked bytes
before investing in a replacement ABI. The final link measures changes
in alignment, shared helpers and branches at that boundary.

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
