# Footprint techniques mapped to the RP2040 tree

This note ranks the techniques that buy function per byte of flash and SRAM
on the RP2040 (ARMv6-M Thumb-1, 264 KB SRAM, 128 KB kernel slot, 144 KB
process window). Each technique is mapped to the place in this tree where it
pays or does not pay. It extends `constrained-c.md`, which owns the compiler
and linker flag analysis (section 2: `--gc-sections`, `-flto`), and
`xip-shared-libc/`, which owns the shared-libc design. The survey began as a
read-only Codex review (`gpt-daybreak-blue-latest`). Its tree citations were
checked against source and its external links re-fetched, and the
corrections are recorded under "Citation corrections".

## Ranked by measured or bounded payoff

1. **Fixed-address XIP libc.** This has the largest flash payoff still open.
   `xip-shared-libc/README.md` measures 502 to 288 packed blocks over 28
   executables, 186 blocks net of the reserved partition. The cost is the
   r9-relative data model in `xip-shared-libc/streams/08-xip-libc-design.md`:
   about 7 percent text growth and a 696-byte GOT. No runnable image exists
   yet.
2. **Multicall membership for small tools.**
   `sys/arch/rp2040/doc/MULTICALL-BSS-OVERLAY.md` records seven boxes
   replacing 615 packed blocks with 182.
   - **Measured on `time`:** joining `utilbox` costs 338 packed bytes and no
     inode, where the standalone costs 8 packed blocks and an inode (7,372
     raw, 6,429 packed bytes).
   - **Packed sizes are not monotone in code size.** heatshrink output moves
     by several bytes either way with layout. `utilbox` crossed a block
     boundary on 12 bytes of raw growth and came back under it with a
     different but equivalent formatting call. A change near a boundary is
     decided by measured packed bytes (`tools/bin/rp2040/hsaout -s`), never
     by text size.
3. **Fill measured alignment holes instead of converting to C bitfields.**
   - **Tick-resolution timers:** `struct proc` has two 2-byte holes in
     `p_alive`, which hold the ITIMER_REAL tick fields (`p_realtick`,
     `p_realitick`). A `_Static_assert` in `sys/arch/rp2040/rp2040/machdep.c`
     pins the size at 104.
   - **New globals:** these sometimes fit existing bss alignment padding. As
     `u_short`, the monotonic clock's `monotick` and `realtick` leave PICO bss
     unchanged; as `int`, they grew it by 32 bytes against `.bss` 32-byte
     alignment.
   - **Why not bitfields:** C leaves bitfield allocation largely to the
     implementation (ISO C N1570 section 6.7.2.1), and on Thumb-1 each
     bitfield store is a read-modify-write. A struct seen through swap,
     ptrace or sysctl needs a layout fixture before any repacking.
4. **Bounded linear scans in place of indexed structures for tables of
   NPROC=25 and NCALL=18 entries.**
   - **ITIMER_REAL:** an earliest deadline that may be stale-early, plus a
     scan of `allproc` only on expiry, cost 416 bytes of PICO text. A
     25-entry heap would need about 100 bytes of RAM plus more code.
   - **Callout queue:** it cannot host per-process timers, because 25
     processes can exhaust 18 slots and `timeout()` panics when the table is
     full.
   - **SRAM:** `ufs-fixed-table-sram-reduction.md` applies the same reasoning
     to SRAM: 10,328 bytes of bss recovered for 612 bytes of text.
5. **Per-artifact flag experiments, then global flags.** `-Oz`, LTO and
   section garbage collection are each measured on the kernel and on each
   box before any change to `share/mk/sys.mk`, which uses `-Os -fno-common`
   with no LTO and no `--gc-sections`.
   - **ICF:** GCC enables `-fipa-icf` by default at `-Os`, so compiler-level
     folding is already in the baseline. The untested parts are its measured
     contribution (an `-fno-ipa-icf` comparison), its reach across
     translation units under LTO, and linker-level folding, which GNU ld
     lacks.
   - **Flag references:**
     - GCC documents `-Oz` and `-fipa-icf` at
       https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html.
     - GNU ld documents `--gc-sections` at
       https://sourceware.org/binutils/docs/ld/Options.html.
   - **Function-pointer tables:** a table (`cdevsw`, `execsw`, a generated
     multicall dispatch) roots every function it names against garbage
     collection, so the measured yield of `--gc-sections` is bounded by those
     tables.
   - **Outliner comparator:** the LLVM machine outliner
     (https://llvm.org/docs/CodeGenerator.html#the-machine-outliner) is a
     Clang-only comparator. On Thumb-1, call overhead and branch reach can
     cancel its savings.
6. **Compressed executables.** HSAOUT with a 512-byte heatshrink window
   (`sys/sys/exec_hsaout.h`, window 9, lookahead 3) stays.
   - **Encoder index:** the heatshrink encoder also runs in the kernel, where
     it compresses SwapRAM pages. Its search index is disabled there to save
     2,048 bytes of kernel bss and a 512-byte `do_indexing` frame against a
     2,100-byte kernel stack, with no change in output bytes
     (`sys/arch/rp2040/heatshrink/heatshrink_config.h`).
   - **Encoder speed:** the recorded cost is 27 percent more encode time on
     the SwapRAM `awk` image, at window 9 and lookahead 8. The slowdown of
     HSAOUT executable packing is unmeasured.
   - **Decoder candidates:** an alternative such as an LZ4 decoder for
     Cortex-M0 (ARM community blog,
     https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/lz4-decompression-routine-for-cortex-m0-and-later)
     is judged on packed root blocks, decoder text and workspace, exec
     latency and worst-case expansion, together.
   - **Decode cycle cost:** the 387-cycles-per-byte model in
     `rp2040-second-core-emulation/` measures the bit-level decoder
     (`get_bits`, `hsx_decoder_poll`) that preceded the word-buffered
     `hsx_dec_run` in `sys/arch/rp2040/rp2040/hsx_stream.c` (commit
     12e6d190). It is a historical baseline, and the current decoder's cost
     per byte is unmeasured.
7. **Bytecode for rarely used tools only where the interpreter already
   ships.**
   - **MicroPython:** its minimal port saves about 20 KB on Thumb-2 by
     omitting the compiler, which still leaves a runtime and heap larger
     than a C applet of a few hundred bytes.
     - minimal-port README:
       https://github.com/micropython/micropython/tree/master/ports/minimal
     - memory model:
       https://docs.micropython.org/en/latest/develop/memorymgt.html
   - **Lua:** `LUA_32BITS` (https://www.lua.org/manual/5.4/manual.html) narrows
     Lua's number representation, not its runtime.
   - **When a family pays:** a family of tools pays for an interpreter only
     when the tools share parser or runtime logic, host compilation removes
     the on-board compiler, and peak heap fits the process window.
     `ondevice-languages.md` lists the interpreters already in the tree.
8. **Event-driven shared stacks.** These do not transfer wholesale, because
   they replace Unix process semantics.
   - **Sources:** TinyOS runs handlers to completion on one stack (Hill et
     al., "System Architecture Directions for Networked Sensors", ASPLOS IX,
     2000, doi:10.1145/378993.379006). Protothreads keep two bytes of state
     per thread (https://dunkels.com/adam/dunkels06protothreads.pdf).
   - **What the port does instead:** it exchanges two 3,072-byte u-areas
     instead of keeping a kernel stack per process
     (`sys/arch/rp2040/doc/CAPACITY.md`). A shared
     buffer with a proven disjoint lifetime is the local form of the same
     idea.
9. **General kernel overlays.** These do not pay here.
   - **Why:** interrupt and exception paths reach kernel code
     asynchronously, overlay metadata and loader code spend the same
     128 KB slot, and flash programming already masks SysTick and needs
     clock catch-up.
   - **Precedent for user-level XIP modules:** NuttX NXFLAT
     (https://nuttx.apache.org/docs/latest/components/nxflat.html). For this
     tree, the fixed libc partition is the better-defined form.

## External ABI precedent for an r9 data base

The AAPCS32 procedure call standard leaves r9 to the platform
(https://github.com/ARM-software/abi-aa/releases/latest/download/aapcs32.pdf).
Tock's `libtock-c` crt0 sets r9 to the application's GOT-relative data at
start-up (https://github.com/tock/libtock-c/blob/master/libtock/crt0.c).
Both inform the `xip-shared-libc/` r9 design.

## Citation corrections

- The TinyOS citation in the original review gave an OSDI 2000 URL that
  returns 404. The paper appeared at ASPLOS IX; the DOI above resolves.
- `developer.arm.com` (DDI 0484, the ARMv6-M Architecture Reference Manual)
  and the ARM community LZ4 post return 403 to automated fetches. Their
  content was not re-read for this note.
- All other external links returned 200 on 2026-11-08.

## Not measured here

- `-Oz`, LTO, an `-fno-ipa-icf` comparison and linker-level folding have
  not been built on this tree; they remain experiments.
- The current decoder's cycles per byte have not been modeled.
- No alternative decoder has been prototyped.
- The fixed-address libc has no runnable image.
