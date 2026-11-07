1. **major** -- Claim: “linking any member attaches the version record” and “a version record checked before the program runs” (`stream:33-34,54`). Evidence: `S3/cmd/sgs/mkshlib/m32/t_btassem.c.unpacked/t_btassem.c:71-92` emits length, pathname offset, and pathname; `S3/uts/3b2/os/exec.c.unpacked/exec.c:715-755` checks bounds, pathname, magic `0443`, and library count. No ABI-version field appears. Correction: call `.lib` a dependency/pathname record; require an explicit ABI major, slot-count floor, and state-layout contract for RP2040.

2. **major** -- Claim: “the fixed data address relies on an MMU” (`stream:44-45`). Evidence: SVR3 `exec.c:606-615` uses an MMU-backed private region, but `sys/arch/rp2040/include/machparam.h:115-127` and `lib/elf32-arm.ld:25` already provide fixed SRAM placement. Correction: distinguish SVR3’s implementation from necessity; justify and measure r9 indirection against reserved fixed SRAM included in process initialization and swapping.

3. **major** -- Claim: “a libc partition is replaced only by a whole-image flash” (`stream:94-96`). Evidence: streaming `lites-1.1/doc/emulator-upgrading:20-46` from `lites-1.1.tar.gz` shows a tested replacement installed by pathname while running programs retain the mapped old file. Correction: preserve immutable referenced bytes; present whole-image flashing as an RP2040 policy choice, not the historical precedent’s only valid update unit.

4. **minor** -- Claim: Palm “library globals are allocated by the library’s Open call” (`stream:74-78`). Evidence: streamed `SystemMgr.h:551-558,993-994` declares `globalsP` and `SysLibOpen`; `LibTraps.h:31-38` defines dispatch ordering. Neither establishes allocation timing or ownership. Correction: cite an implementation or specification, or label Open-time allocation as proposed.

5. **minor** -- Claim: “direct symbols freeze every internal address” (`stream:26-27`). Evidence: `S3/lib/shlibc/spec.unpacked/spec:371-388` distinguishes exported from hidden symbols. Correction: direct imports freeze referenced exported addresses; hidden internals may move.

6. **minor** -- Claim: COHERENT shares images “by segmentation” (`stream:102-103`). Evidence: streamed `i386/mmu.c:142-187,217-230` installs page-table entries and flushes the paging TLB. Correction: describe executable-segment sharing backed by 386 paging.

7. **minor** -- Claim: Pistachio contains “29 members, 787 bytes” (`stream:99-100`). Evidence: `tar -tzf ... | wc -l` returns 45 entries; verbose regular-file rows yield 29 files totaling 787 bytes. Correction: say “29 placeholder regular files totaling 787 bytes within 45 tar entries.”

8. **minor** -- Claim: “OKL4 3.0 holds no … dynamic-linking material” (`stream:100`). Evidence: the streamed archive contains shared-library construction in `tools/SCons/Tool/link.py:49-54` and ELF dynamic structures in `elf.h:264-269,468-469`. Correction: limit the absence claim to searched target-runtime mechanisms.

9. **minor** -- Claim: `baduaddr_span` applies “seL4’s discipline” (`stream:136-138`). Evidence: streamed seL4 `src/kernel/cspace.c:164-183` validates capability guard/radix bit counts; DiscoBSD `usercopy.h:30-33` performs byte-span containment. Correction: present only a bounded defensive-programming analogy.

10. **minor** -- Claim: “A Thumb-1 register shift by 32 or more yields zero” (`stream:138-140`). Evidence: the RP2040 instruction table separates `LSLS`, `LSRS`, `ASRS`, and `RORS`; arithmetic right shift and rotation do not share that universal result. Correction: cite ARMv6-M pseudocode and state operation-specific register-count semantics.

11. **minor** -- Claim: the ROM floating-point resolver “runs on this target today” (`stream:124-131`). Evidence: `rom_float_resolver.S:21-49,128-129` and `rom_float.inc:41-74` establish validation, resolution, and cached dispatch in source, but no dated execution receipt was cited. Correction: provide board logs, binary hashes, and exercised entries, or describe implemented source behavior.

12. **minor** -- Claim: the MPU repair and guard “both reprogram a region at run time” (`stream:132-135`). Evidence: `MPU.md:296-320` records a coverage gap and proposed repair/gate. Correction: keep runtime reprogramming prospective until implementation, register readback, and fault-test evidence exist.

13. **minor** -- Claim: an L4 X.2 flexpage precedent supports the MPU comparison (`stream:141-143`). Evidence: the stream gives neither a local manual path nor section locator. Correction: supply reproducible locators and qualify the analogy because MPU subregion-disable coverage need not equal one contiguous aligned flexpage.

The fixed SVR3 addresses, ranged jump slots, hidden exports, pointer-indirected imports, unresolved-entry failure, magic/count checks, and `SHLBMAX=2` survive. SunOS version selection, OSF pre-relocation, Palm dispatch/state structure, 2.11BSD thunks, Xinu stack checks, and the ROM resolver’s source-level mechanism also survive. The survey supplies useful precedents, but it establishes neither RP2040 execution conformance nor an advantage over multicall boxes or relocation. The review covers current stream SHA-256 `0cc65130...`; the stream changed from `6f5e45d4...` during review, then remained stable across repeated checks. Review commands wrote no files, and archive contents were read only through streaming operations.

