# Stream: precedent in a curated historical UNIX source archive

Read-only audit of a local curated mirror of historical UNIX source (AT&T,
Sun, OSF, TUHS, Mach, L4, Palm, Xinu), navigated through its catalogs with
streaming reads (`tar -t`, `unzip -l`, `tar -xO`, `pdftotext`). Paths are
relative to the archive root; `S3` stands for
`extracted/archives/att/sysvr3/31/usr/src`, whose `.Z` files have plain text
beside them under `X.unpacked/X`. The orchestrator re-read items 1 and 4.

## Ranked findings

1. SVR3 `libc_s` specification (source), the closest precedent for the whole
   design. `S3/lib/shlibc/spec.unpacked/spec`: lines 5-7 fix
   `#address .text 0x80040000`, `#address .data 0x800e0000` and
   `#target /shlib/libc_s`; `#branch` (line 8 on) assigns numbered slots
   (`__doprnt 1` ...), and `write 129-150` (line 137) reserves a slot range so
   the table can grow without moving later entries (ranges parsed in
   `rdspec.c:260-290`). `#objects` (line 139 on) orders members by call
   frequency and locality. `#init` blocks (343-369) bind library-side
   pointers. `#hide linker *` and `#export linker` (376-377) hide every symbol
   except branch-table entries, `#init` targets and explicit exports such as
   `errno`. The SVR4 i386 spec
   (`extracted/archives/att/sysvr4/svr4/lib/libc/i386/spec`) repeats the form.
   Bearing: supports a fixed-address library and answers the versioning
   question with a numbered, append-only, range-reservable jump table plus a
   hide-by-default export policy. Direct imports freeze the addresses of the
   exported symbols they reference; hidden internals may still move.
2. SVR3 `mkshlib` host and target libraries (source). Branch-table entries are
   bare jumps (`S3/cmd/sgs/mkshlib/m32/t_btassem.c`, lines 46-57). The host
   library is a stub archive of NOLOAD sections at the slot addresses, so
   address conflicts surface at link time. Every member references a "lib
   def" object carrying the `.lib` record, so linking any member attaches a
   dependency record (`h_create.c:23-34,73-76`) holding a length, a name
   offset and the target pathname (`t_btassem.c:71-92`); it has no ABI-version
   field (a search of `mkshlib` for "version" finds only unrelated comments in
   `h_fcns.c:60-63`). `t_mkbt.c:67-263` fails on an unresolved branch entry.
   Bearing: import stubs can be a generated archive of absolute-address stubs
   whose dependency record arrives by an undefined-symbol reference, with no
   runtime linker; the RP2040 record must add its own ABI major, slot-count
   floor and state-layout contract.
3. SVR3 libc data indirection (source).
   `extracted/archives/att/sysvr3/301/usr/src/lib/libc/inc/shlib.h:14-36`
   rewrites library references to program-owned data as pointer indirections
   (`#define _ctype (* _libc__ctype)`, `#define _cleanup (* _libc__cleanup)`),
   bound by `#init`. libc's `.data` is a per-process private region the kernel
   attaches at its fixed address (`S3/uts/3b2/os/exec.c:606-615`). Bearing:
   fixed-address text with pointer-indirected state is proven. SVR3 implements
   the fixed data address with an MMU-backed private region, which does not
   make it necessary here: the RP2040 window is already fixed
   (`sys/arch/rp2040/include/machparam.h:115-127`, `lib/elf32-arm.ld:25`), so
   a fixed library-data area inside the window is an alternative to the
   r9-relative GOT, chosen between by measuring indirection cost against
   reserved SRAM and its effect on initialization and swap images.
4. SVR3 kernel attach (source). `S3/uts/3b2/os/exec.c` requires library magic
   0443 (line 739), validates the `.lib` record (715-719, ELIBSCN), and caps
   libraries per process (445-452, 1005, ELIBMAX);
   `extracted/archives/att/sysvr3/32/usr/src/uts/3b2/master.d/kernel.unpacked/kernel:215`
   sets `SHLBMAX = 2`. The same ELIBACC, ELIBBAD, ELIBSCN, ELIBMAX and
   ELIBEXEC names appear in OKL4 3.0's `libs/c/include/errno.h:185-189`.
   Bearing: cap libraries per process, check the dependency record at exec,
   use the ELIB errno names; `chkshlib`
   (`S3/cmd/sgs/chkshlib/common/main.c`, `check.c:19-35`) is the model for an
   offline compatibility gate, since the on-disk record carries no version.
5. OSF/1 (documentation and source). OSF/1 Internals volume 2, shared-library
   module 10-22/10-23, names three techniques: copy-on-write relocation, PIC,
   and pre-relocated libraries at fixed locations linked without further
   relocation. Digital OSF/1 1.10 `usr/lib/makefiles/standard.mk:350` builds
   with `-set_version` and a central `so_locations` address registry;
   `kernel/bsd/kern_mman.c:638-673` implements `mvalid(addr, len, prot)`, which
   checks a user range against map protections. Bearing: supports fixed
   placement; `mvalid` is the model for admitting libc flash as a read-only
   source while keeping it invalid as a write target.
6. SunOS 4.1.3 `ld.so` (source, `sources/sun/os/sunos/4.1.3/.../lang/rtld/`):
   `libX.so.MAJOR.MINOR` with an exact major match and the highest minor
   (`cache_common.c:231-254,301-314,406-415`); crt0 calls the runtime linker
   only when `__DYNAMIC` is non-null (`lib/csu/sparc/crt0.s:64-68`). Bearing:
   major as the ABI number baked into stubs, minor as the slot count; a crt0
   that relocates only when needed. The mapping mechanism needs an MMU and
   does not transfer.
7. Palm OS (headers in the emulator 3.5 source zip,
   `SrcShared/Palm/Platform/Incs/`). `SysLibTblEntryType` declares
   `dispatchTblP` and `globalsP` (`Core/System/SystemMgr.h:551-558`);
   `SysLibOpen` is declared at `SystemMgr.h:993-994`; `Libraries/LibTraps.h:31-38`
   reserves slots 0-4 (name, open, close, sleep, wake). The headers say
   neither when `globalsP` is allocated nor who owns it, so allocation at Open
   is a proposed convention, not a documented Palm fact. `Core/CoreTraps.h:822`
   records a trap number reused across releases, a warning against
   renumbering slots. Palm's ROM XIP source is absent.
8. 2.11BSD `ld -Z` thunks and PC-Xinu stack checking (source).
   `bin/ld/ld.c` from the 2.11BSD pl195 source tarball: `THUNKSIZ 8` (line 126),
   overlay-resident text symbols redirected to 8-byte thunks in base text
   (1028-1036), thunk emission (1467-1487). PC-Xinu
   (`extracted/archives/xinu/pc-xinu/XINU-68K.TAR/src/sys/sys/resched.c:30-70`,
   `create.c:59-61`) checks a stack-base canary and the stack pointer against
   its limit at each context switch under `STKCHK`. Bearing: a thunk table in
   a small a.out linker is the model for board-linker veneers; a canary at the
   window floor checked on syscall return is a fallback stack guard when no MPU
   region is spare.

## Other findings
- Lites 1.1 links its emulation library at a fixed base
  (`emulator/programs/emulator_base.c:31-60`). Its upgrade procedure
  (`doc/emulator-upgrading:20-46`) copies the new image beside the old one,
  tests it, and renames it into place by pathname; running programs keep the
  old mapped file, which is never overwritten. Bearing: preserve the
  immutable bytes running programs reference; a whole-image reflash is an
  RP2040 policy choice, not the only update unit the precedent allows.
- L4Ka RC2 (Hazelnut era, StrongARM ports, all with an MMU) redirects kernel
  imports through function pointers (`kernel/include/arm/farcalls.h:29-35`).
  The standalone Pistachio export is 29 placeholder regular files totaling
  787 bytes within 45 tar entries; a Pistachio kernel and user snapshot sits
  inside OKL4 3.0 under `tools/magpie/test/fullsystem/pistachio/` (1,350
  entries, listed but not read). OKL4 3.0's target runtime holds no MPU or
  no-MMU port and no target dynamic loader; shared-library construction
  appears only in build tooling (`tools/SCons/Tool/link.py:49-54`) and ELF
  dynamic structures (`elf.h:264-269,468-469`). Iguana ships no shared
  libraries. seL4 10.1.1 is page-table based with no MPU port.
- Coherent's `coh.386/exec.c:407-448` handles the 286 `l.out` shared-image
  format (`L_SHRI`, `L_SHRD`); the 386 kernel backs segments with page tables
  (`i386/mmu.c:142-187`). This is executable-segment sharing on paging, not a
  library mechanism.

## Not found or not opened
MINIX 1/2 `mm/exec.c` and `find_share` (the archive's MINIX is a userland
tree only); Linux a.out jump-table DLL tools; Xenix, UnixWare and USL shared
libraries (binary images or unsearched SVR4 duplicates); Ultrix; MachTen
source; LSX and Mini-UNIX beyond raw filesystem images; Inside Macintosh;
the 2.11BSD overlay runtime handler; the ARMv6-M Architecture Reference
Manual.

## Addendum: in-tree precedent and MPU-map tension
- `lib/libc/arm/gen/rom_float_resolver.S` is the closest in-tree precedent.
  It validates the boot ROM's public magic, compatibility byte and version
  floor (line 21), refuses to publish a cache cell outside
  `[USER_DATA_START, USER_DATA_END)` (line 39), and resolves each target once
  through a cold trampoline. Retained board evidence: `MPU.md` records
  `fptest` printing `FPTEST OK` and mputest's "rom float divide" probe passing,
  and the earlier SIGSEGV at ROM pc 0x00002cec before the divider entered the
  map; `tests/rp2040/fptest/README.md:9` and `TESTING.md:541` describe a
  bit-exact cold and cached corpus. The transcript is undated and carries no
  kernel or executable digest (the commit adding it, 3c1358cf, is dated
  2026-09-20), so it counts as source behavior plus one transcript. The
  resolver models lazy binding with a version floor; the SVR3 table models a
  link-time-bound, version-stable ABI.
- `MPU.md` states the map is programmed once at startup. Its "Known gap"
  section records the LARGE-epoch coverage gap and proposes a repair that
  toggles a region with readback; a break-following stack guard would also
  write a region. Both are prospective until implementation, register
  readback and a fault test exist.
- `baduaddr_span` (`machdep.c:919-924`) uses a non-wrapping containment test
  (`usercopy.h:26-33`). seL4's `resolveAddressBits`
  (`src/kernel/cspace.c:164-183`) checks capability guard and radix bit
  counts. The shared idea, checking a length before comparing, is a
  defensive-programming analogy, not a derivation. C leaves shifts by the
  type width or more undefined, and each Thumb-1 register shift (`LSLS`,
  `LSRS`, `ASRS`, `RORS`) defines its own large-count result; audit variable
  shifts in span or MPU-size arithmetic against the ARMv6-M pseudocode.
- The L4 X.2 reference manual (revision 7, section 4.1, "Fpage") defines an
  fpage as (base, 2^s, rights) with the base aligned to the size, and treats
  fpages below the hardware minimum as nilpages. An MPU region has a similar
  shape, but subregion-disable bits can enable a non-contiguous set of
  eighths, which no single aligned fpage expresses.

## Advisory corrections
- The audit stated that ARMv6-M lacks MPU subregions. PMSAv6 on the
  Cortex-M0+ provides eight subregion-disable bits per region of 256 bytes or
  more, which `mpu.c` already uses for the SIO divider region; the 32-byte
  user stack guard in stream 07 depends on them. Rejected.
- The `SHLBMAX` citation named a path without its `.unpacked` component; the
  corrected path is given in item 4.
- The audit wrote and deleted one scratch file outside the session scratchpad;
  nothing in the archive changed.

## Resolution of the adversarial review

Verified by the stream's originating agent through streaming reads and
applied by the orchestrator, because the agent's role had no write access.

1. ACCEPT. `t_btassem.c:71-92` emits length, name offset and pathname only;
   item 2 calls `.lib` a dependency record and requires an explicit ABI major.
2. ACCEPT. SVR3's private data region (`exec.c:606-615`) is an implementation;
   the RP2040 window is fixed (`machparam.h:115-127`, `elf32-arm.ld:25`).
3. ACCEPT. The Lites upgrade renames a tested replacement into place while
   running programs keep the old file.
4. ACCEPT. No allocation-time evidence for `globalsP`; Open-time allocation is
   a proposed convention.
5. ACCEPT. `spec:371-388` separates exported from hidden symbols.
6. ACCEPT. Coherent shares 286 `l.out` segments on 386 paging.
7. ACCEPT. 45 tar entries, 29 regular files, 787 bytes.
8. ACCEPT, extended. OKL4 build tooling and ELF structures confirmed; the
   1,350-entry Pistachio snapshot inside OKL4 corrects "placeholder only".
9. ACCEPT. The seL4 comparison is an analogy.
10. ACCEPT, with a limit. The ARMv6-M manual is absent from the archive; the
    universal shift claim is withdrawn pending its pseudocode.
11. ACCEPT. Retained board transcript in `MPU.md`, undated and undigested.
12. ACCEPT. Runtime reprogramming is prospective.
13. ACCEPT. Locator given; subregion coverage is not one aligned fpage.
