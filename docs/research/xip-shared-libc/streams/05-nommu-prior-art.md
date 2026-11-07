# Stream: no-MMU position-independent code and shared libraries

S = read from a primary source; M = measured by the stream's agent; I =
inferred.

1. uClinux bFLT. S: `fs/binfmt_flat.c` (Linux master) supports RAM, GOTPIC,
   GZIP and GZDATA flags. Without the RAM and GZIP flags on a no-MMU kernel
   the loader requests a file mapping of the text with `vm_mmap()`; a source
   comment says the mapping "should" yield a ROM pointer and that the loader
   does not care if it does not (`fs/binfmt_flat.c`, text-mapping branch). In
   `mm/nommu.c` a regular file defaults to a copied mapping (`NOMMU_MAP_COPY`)
   unless the file operations report direct-mapping capability and provide
   `get_unmapped_area`. XIP of bFLT text is therefore conditional on the
   backing file and filesystem mapping support; the flag choice only requests
   it. Data, GOT and bss copy to RAM. The old shared-library scheme (Linux
   v5.17, `MAX_SHARED_LIBS` of 4) gave the executable library ID 0 plus at most
   three shared libraries, each in a fixed slot, and encoded the library ID in
   relocation bits 31..24. Commit 70578ff3367d ("binfmt_flat: Remove shared
   library support", 2022) removed it as unused outside old m68k. Linux ARM
   no-MMU requires CPU_V7M; `arch/arm/mm/Kconfig` has no ARMv6-M option.
2. ARM FDPIC. S: kernel support by Nicolas Pitre (2017); GCC support by Lyon and
   Guene documented for GCC 10. GCC master `arm.cc` option override:
   `if (TARGET_THUMB1) sorry ("FDPIC mode is not supported in Thumb-1 mode")`.
   GCC documents that -mfdpic does not support static linking. Cortex-M0+ is
   excluded.
3. Compilers on Cortex-M0. S: GCC documents -msingle-pic-base,
   -mpic-register (r9 default on EABI), -mpic-data-is-text-relative; with a high
   PIC register Thumb-1 copies it to a low register in each function. Clang
   accepts -fropi/-frwpi on Thumb; RWPI reserves r9; ROPI rejects C++
   ("ROPI is not compatible with c++"). M: a Clang 23.1.4 probe of
   `const char text[]="x"; const char *pointer=text;` with
   `--target=thumbv6m-none-eabi -Os -fropi` exits 0 for C and emits a
   `R_ARM_ABS32` relocation against `text` in `.data`, so ROPI accepts the C
   source but the object still holds an absolute address that a loader must
   treat as a relocation. The same source as C++ exits 1.
   M: compile-only object sizes, DiscoBSD main checkout at commit 122ec634,
   over the 189 sources `lib/libc/{stdio,stdlib,gen,string}/*.c`, flags
   `-mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections
   -fno-common -Iinclude -w -c` (Clang adds `--target=thumbv6m-none-eabi`),
   section totals from `arm-none-eabi-size` (text includes rodata), summed
   over the objects every variant of one compiler built. GCC 16.2.0 built 181
   of 189 (failures: gen/abs, ecvt, fstab, insque, ndbm, remque, setmode,
   valloc); Clang 23.1.4 built 185 of 189 (failures: gen/abs, insque, remque,
   valloc). Text bytes, GCC, 181 objects: base 39076; `-fPIC -msingle-pic-base
   -mpic-register=r9 -mno-pic-data-is-text-relative` 41902 (+7.2 %); plain
   `-fPIC` 41738 (+6.8 %). Clang, 185 objects: base 51290; `-fropi` 51698
   (+0.8 %); `-frwpi` 52750 (+2.9 %); both 53138 (+3.6 %). Data and bss do not
   change under Clang; GCC data grows from 955 to 1031. The two compilers'
   bases differ because their matched sets differ and their code generation
   differs, so cross-compiler percentages carry no ranking. No link and no
   run: the linker's ROPI/RWPI support and the load-time cost are unmeasured.
   The command is a loop of one compile per source and variant, aggregated by
   `arm-none-eabi-size`; it replays from the commit above. GCC warns "thumb-1
   mode PLT generation not currently supported", so -shared PLT libraries fail
   on M0.
4. Tock, NuttX, Zephyr. S: libtock-c builds cortex-m0 apps with
   `-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative
   -Wl,--emit-relocs`. Tock keeps application text in flash and relocates RAM
   state in the application's own startup: `libtock/crt0.c` sets r9 from the
   header's GOT offset plus the RAM start, then `_c_start_pic` rewrites GOT
   entries (a high-bit sentinel separates flash targets, rebased on the flash
   application start, from RAM targets, rebased on the RAM start), copies the
   data section from flash and applies data relocations. The kernel loader
   supplies the flash and RAM addresses; the fixups run in the application
   process. The source gives no evidence that the relocation set is small; it
   is the GOT plus data-pointer list. Tock's 2025 workshop kept standard PIC
   over ROPI/RWPI. Zephyr LLEXT has a 16-byte Thumb-1 veneer for ARMv6-M
   under CONFIG_LLEXT_VENEERS and loads relocatable objects (not PIC). NuttX
   NXFLAT is bFLT-like with a GOT and per-task data; current NuttX documents
   and `arch/arm/src/common/Toolchain.defs` specify
   `-msingle-pic-base -mpic-register=r9`; r10 is the earlier module ABI
   generation, which the docs record as replaced by the r9 generation. The NuttX
   docs say NXFLAT is not a true shared-library scheme and document only
   ARM and Cortex-M3/4 (Thumb-2) toolchains; ARMv6-M support is unverified.
5. RP2040 XIP. S: datasheet 2.6.3.1 (printed page 123), 16 KB two-way
   set-associative cache with a 1-cycle hit. The aliases differ: `0x10`
   cacheable and allocating (normal), `0x11` cacheable and non-allocating
   (hit check, no fill on miss), `0x12` non-cacheable and allocating (no hit
   check, always fill), `0x13` non-cacheable and non-allocating (complete
   bypass). A cache-bypass control uses `0x13`. Flush just over 1024 cycles
   (2.6.3.2); two counters (2.6.3.5, printed page 125) count all XIP accesses
   and XIP cache hits; they report neither elapsed cycles nor which code
   caused an access. The miss penalty is not given; a forum estimate of about
   80 system clocks per 32-bit miss is unverified.

FOR PIC plus exec-time relocation into RAM: shipping precedent (Tock on M0,
with flash text and application-startup fixups of RAM state; whether RAM text
is a useful variant is untested); +7 % text with GCC measured at object level.
AGAINST: RAM is the scarce resource; PIC alone shares nothing; a reserved
register; function-pointer tables need fixups; new format and loader work.

FOR a fixed-address or ROPI/RWPI XIP libc: removes libc text from the RAM of
the programs that relink against it (28 of 30 programs in the union-library
relink and 26 of 30 in the whole-library relink of
`measure/relink-results-union.txt` and `relink-results-whole.txt`; adminbox
and sh fail both, ps and init fail the whole-library case); 1-cycle cache
hits. Total stored bytes add the shared image once to the per-program sizes,
which the relink tables omit. AGAINST: GCC lacks ROPI/RWPI; FDPIC excluded on
Thumb-1; the 16 KB cache is shared with the kernel and the miss penalty is
unmeasured; a fixed-slot ABI needs versioning.

Deciding measurement: a board run of libc workloads executing from
`0x10xxxxxx` against SRAM. The XIP counters give the hit rate only, so the run
also records elapsed cycles (a cycle counter or timer read around a fixed
workload), compares total stored bytes (programs plus the shared image) and
peak RAM per configuration, includes loader startup, and controls the clocks,
interrupt load, workload output and cold versus warm cache state, with a
`0x13` bypass run as the uncached control.

## Resolution of the adversarial review

1. ACCEPT: datasheet 2.6.3.5 lists two counters (all XIP accesses, cache hits) with no cycle or attribution counter; the deciding measurement now adds elapsed cycles, storage, peak RAM, loader startup and controls.
2. ACCEPT: rerun at commit 122ec634 reproduced every figure (GCC 181 of 189 built, Clang 185 of 189, same section totals); the body now records flags, source set, failures, denominators and the aggregation tool; no object manifest or script is retained in the repository, so this stream carries the replay recipe only.
3. ACCEPT: `libtock/crt0.c` sets r9 in application startup and `_c_start_pic` fixes the GOT, copies data and applies data relocations; the body now says flash-text PIC with application-startup fixups and drops the small-relocation-set claim.
4. ACCEPT: datasheet 2.6.3.1 defines `0x11` as cacheable non-allocating and `0x12` as non-cacheable allocating; only `0x13` bypasses; the body names each.
5. ACCEPT: `fs/binfmt_flat.c` comment says the mapping "should" give a ROM pointer; `mm/nommu.c` defaults regular files to `NOMMU_MAP_COPY` and clears direct mapping without `get_unmapped_area`; XIP is now conditional.
6. ACCEPT: `measure/relink-results-union.txt` has two FAIL rows (adminbox, sh) in 30 programs and `relink-results-whole.txt` four (adminbox, sh, ps, init); claims now read 28 of 30 and 26 of 30, with the shared image added once for total storage.
7. ACCEPT: a Clang 23.1.4 `-fropi` probe exits 0 for C with an `R_ARM_ABS32` against `text` in `.data` and exits 1 for C++ ("ROPI is not compatible with c++"); the body separates the two.
8. ACCEPT: `Toolchain.defs` line 597 sets `-mpic-register=r9`, and `nxflat.rst` calls r9 the v2 module ABI that replaced r10; the body states r9 as current; Cortex-M3/4 only remains documented.
9. ACCEPT: Linux v5.17 `binfmt_flat.c` defines `MAX_SHARED_LIBS` as 4, loads the executable as ID 0 and loops over IDs 3 through 1 for libraries; the body says the executable plus at most three shared libraries.
