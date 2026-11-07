# Stream: BSD source trees (local BSD_Workspace)

Paths are workspace-relative: each begins with the tree directory (for example
`retrobsd/sys/include/exec_aout.h`). O = observed in
source; I = inferred.

## No-MMU and small BSDs
- 211bsd. O: magics 0407, 0410, 0411, 0405 and auto-overlay 0430/0431
  (`211bsd/sys/h/exec.h:39-44`); all text is sticky on an LRU list
  (`211bsd/sys/sys/vm_text.c:55-61`); up to 15 overlays with ld -Z thunks in the
  base segment (`211bsd/bin/ld/ld.1:240-258`, `211bsd/sys/sys/kern_exec.c:501-546`). No relocation at exec,
  no shared library.
- retrobsd (PIC32MX, no MMU). O: exec_aout accepts OMAGIC only, folds text
  into data, loads at fixed USER_DATA_START (`retrobsd/sys/kernel/exec_aout.c:28-34,55-58`;
  `retrobsd/sys/pic32/machparam.h:97-99`; `retrobsd/src/elf32-mips.ld:22`).
  exec_elf takes one RWX PT_LOAD at p_vaddr (`retrobsd/sys/kernel/exec_elf.c:111,135-143`).
  EX_PIC and EX_DYNAMIC flags are defined but unimplemented, and the flag check
  is vacuous (`retrobsd/sys/include/exec_aout.h:30-31,54,78`). No multicall:
  `retrobsd/src/cmd/Makefile:13-60` enumerates 155 compiled command targets
  (SUBDIR 88, STD 56, NSTD 3, SETUID 2, OPERATOR 1, KMEM 3, TTY 2), each linked
  statically against libc.a. That is a target count, not a count of installed
  binaries. This is DiscoBSD's direct ancestor model.
- LiteBSD (PIC32MZ, MMU). O: ZMAGIC and static ET_EXEC only; text mapped
  read-only and shared through the vnode (`LiteBSD/sys/kern/kern_exec.c:330,350-390,585-603`).

## a.out shared libraries
- 4.4BSD-Lite2 kernel has no shared-library support. The donated SunOS 4 code
  in `4.4BSD-Lite2/contrib/sun.sharedlib/` is unbuilt (sparc, m68k only); its README lists a
  PIC GOT bug with static initialized variables and calls .sa stubs "a crock";
  rtld relocates itself first with no symbolic relocations (`4.4BSD-Lite2/contrib/sun.sharedlib/lang/rtld/rtld.c:219-232`).
- 386bsd accepts ZMAGIC only (`386bsd/usr/src/kernel/kern/execve.c:175-181`). QMAGIC appears only in
  later trees; NetBSD still carries ld.aout_so.

## crunchgen and crunchide
- Rationale in all three trees is size: fitting programs onto an installation
  or recovery floppy (`freebsd-src/usr.sbin/crunch/crunchgen/crunchgen.1:50`,
  `openbsd-src/usr.sbin/crunchgen/crunchgen.8:58`,
  `netbsd-src/usr.bin/crunch/crunchgen/crunchgen.1:51`; `netbsd-src/usr.bin/crunch/README:44-49`
  "an old space-saving technique"; `freebsd-src/rescue/README:11`).
- Dispatch by argv[0] through `entry_points[]`
  (`freebsd-src/usr.sbin/crunch/crunchgen/crunched_main.c:67-110`), the same
  shape as DiscoBSD's dispatch table.
- Hiding: FreeBSD and NetBSD crunchide rename globals to `_$$hide$$ file sym`
  rather than localize them, because localizing broke MIPS CALL16 relocations
  (`freebsd-src/usr.sbin/crunch/crunchide/exec_elf32.c:207-215`). OpenBSD's `openbsd-src/usr.sbin/crunchgen/elf_hide.c:314-327` sets STB_LOCAL, the
  same effect as `objcopy --keep-global-symbol`.
- crunchide's TODO (`freebsd-src/usr.sbin/crunch/crunchide/crunchide.c:55-57`): start every tool's BSS at one address
  so the image pays the maximum rather than the sum. Unimplemented upstream;
  DiscoBSD's multicall BSS overlay implements it.
- FreeBSD rescue: static, MK_PIE=no, "Static-PIE is not supported"
  (`freebsd-src/rescue/rescue/Makefile:4-17`). NetBSD rescue optionally links static PIE
  (`netbsd-src/rescue/Makefile:14-21`).

## Static PIE and ARM relocation sets
- OpenBSD rcrt0 `_dl_boot_bind` (`openbsd-src/lib/csu/boot.h:76-170`)
  handles exactly R_ARM_RELATIVE, GLOB_DAT and ABS32 on ARM
  (`openbsd-src/libexec/ld.so/arm/archdep.h:39-50`). The build disables the stack
  protector before relocation on every non-amd64 architecture, ARM included
  (`openbsd-src/lib/csu/Makefile:34-39`). The `-fno-builtin` and
  `-fno-jump-tables` restrictions apply only to mips64 and powerpc variants
  (`Makefile:41-54`); whether ARM objects contain builtin calls or jump tables
  is not established here and needs an object inspection. The motive for static
  PIE is ASLR (I).
- OpenBSD ARM ld.so flags Thumb relocations as errors
  (`openbsd-src/libexec/ld.so/arm/rtld_machine.c:50-80`). NetBSD ld.elf_so self-relocation
  is `*where += relocbase` per REL entry (`netbsd-src/libexec/ld.elf_so/arch/arm/mdreloc.c:35-55`). That loop uses one
  `relocbase` both to locate each target and to adjust its value, so it assumes
  a single load bias for all relocated words.

## No-MMU, FDPIC, single address space
- NetBSD's GCC tree: `sorry ("FDPIC mode is not supported in Thumb-1 mode")`
  (`netbsd-src/external/gpl3/gcc/dist/gcc/config/arm/arm.cc:3619-3624`). Ordinary
  PIC is configured separately (`arm.cc:3598-3604`) and is not rejected on
  Thumb-1. Thumb-1 copies a high PIC register into a low pseudo register per
  function (`arm.cc:7984,8012-8014`). O (checked with GCC 16.2.0): compiling a
  global-data read with `-mcpu=cortex-m0plus -mthumb -Os -fpic -ffreestanding`
  succeeds. The output loads `_GLOBAL_OFFSET_TABLE_-(.LPIC0+4)` and
  `shared_value(GOT)` from a literal pool, computes the GOT address with
  `add r3, pc`, then performs two dependent loads (GOT entry, then the value).
  Per-access cost, GOT size and relocation count for a full libc remain
  unmeasured.
- rtems-libbsd: no dynamic loader; FreeBSD commands link into one image and
  run in-process with setjmp/longjmp exit and per-program fd, memory and
  destructor tracking (`rtems-libbsd/rtemsbsd/rtems/rtems-program.c:240-328`).
- No PIC overhead measurements for Thumb anywhere in these trees.

## Bearing on DiscoBSD
- Against PIC plus relocation: the GCC implementation rejects FDPIC on
  Thumb-1 (ordinary single-base `-fpic` does compile, with GOT-indirect data
  access, so ordinary PIC is not ruled out by compiler support, only by
  unmeasured cost); no BSD
  precedent relocates on a no-MMU Thumb target; the fixed window removes the
  co-residence motive; BSD's documented size answer is crunching.
- For: a RELATIVE-only fixup pass is small, under these conditions: one common
  load bias, writable relocation destinations, and a verified RELATIVE-only
  relocation set. The XIP libc layout of the measure stream (text at
  0x10100000, data at 0x20000000, `measure/libcx.ld:3-8`) has two independent
  placements, so the uniform-bias NetBSD loop alone does not fit; the pass must
  be segment-aware, with separate text and data biases, or text must stay
  free of absolute words. retrobsd's a.out header already reserves relocation
  fields and flag bits.
- No source in these trees ranks ordinary PIC against a fixed-address libc.
  The survey justifies investigating the fixed-address design beside multicall;
  it does not show that it beats PIC in flash, SRAM, relocation or execution
  cost.
- Alternatives (I): a fixed-address non-PIC libc with a jump table avoids
  relocation entirely; 211bsd thunk overlays address text larger than a window.

## Resolution of the adversarial review
1. ACCEPT. `arm.cc:3598-3604` configures ordinary PIC and `arm.cc:3619-3624` rejects only FDPIC; a GCC 16.2.0 Cortex-M0+ `-fpic` compile exits 0 with GOT-relative literal-pool loads. Body corrected; PIC cost comparison marked unmeasured.
2. ACCEPT. `mdreloc.c:35-55` uses one `relocbase` for target and value; `measure/libcx.ld:3-8` places text at 0x10100000 and data at 0x20000000. Body now states the conditions and requires a segment-aware pass.
3. ACCEPT. `openbsd-src/lib/csu/Makefile:34-39` covers ARM for the stack protector only; lines 41-54 limit `-fno-builtin` and `-fno-jump-tables` to mips64 and powerpc. Body corrected.
4. ACCEPT. Parsing `retrobsd/src/cmd/Makefile:13-60` gives 88+56+3+2+1+3+2 = 155 targets; the "about 100" claim is replaced and distinguished from installed binaries.
5. ACCEPT. `386bsd/usr/src/kernel/kern/execve.c:175`, `retrobsd/sys/include/exec_aout.h:54,78` and `211bsd/sys/h/exec.h:39-44` verified; all paths now carry their tree prefix and the 211bsd range is fixed.
