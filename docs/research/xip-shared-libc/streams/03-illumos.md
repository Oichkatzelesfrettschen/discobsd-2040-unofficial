# Stream: illumos and Solaris

Read-only web research; no local illumos source. OBS = read on the cited page
(some pages reached through a summarizing fetch, so wording is paraphrase
unless quoted); INF = inference.

1. Why libc.a went away. OBS: Solaris 10 and later ship no 32-bit system
   archives; static executables are "no longer achievable without specialized
   system knowledge" (Oracle Solaris 11.4 Linkers and Libraries Guide, "Static
   Executables"). The Solaris 9 end-of-support notes: only applications
   dynamically linked against the system libraries that provide the ABI are
   designed for future compatibility. Rod Evans (linker-aliens.org, "static
   linking, where did it go"): libc is the user/kernel interface; a static
   link freezes one release's trap details into the binary; folding libthread
   into libc made partial static linking unworkable. INF: the argument assumes
   kernel and libraries upgrade independently of applications. For the
   commands DiscoBSD ships, kernel, libc and commands arrive as one image, so
   the argument transfers weakly there. It transfers fully to persistent
   user-built executables: the root filesystem is writable and ships a native
   `cc`/`as`/`ld` chain that links programs against `libc.a`
   (`sys/arch/rp2040/doc/STORAGE.md`, "What ships"), and such a binary can
   outlive a kernel or library-only update. A shared libc therefore needs
   compatibility and rejection rules for user binaries (version tag, or a
   refuse-to-run check on a mismatched libc).
2. Costs. OBS (Guide, chapter 4, position-independent code): a text
   relocation remaps text writable, reserves swap and gives the process a
   private copy, so text stops being shared; "-z text" fails a link with text
   relocations. A table of addresses declared `const char * const tab[]`
   causes a text relocation when the toolchain places the pointers in a
   read-only segment (Guide, "Move Read-Only Data to Text"); the same page
   notes that some PIC compilers detect such assignments and place them in
   writable `.picdata`, so the section placement and relocations of the actual
   object decide, and `readelf -r` on the build shows which case holds.
   Immediate relocations finish before main; lazy PLT binding spreads the
   cost. Combined relocation sections group RELATIVE entries for bulk
   processing through DT_RELACOUNT; current `ld(1)` in illumos-gate states
   that merging is now the default and `-z combreloc` is accepted for older
   build environments and has no effect, so the option is historical syntax.
   The Guide gives no per-relocation cost; the only numbers are SPARC GOT
   limits (2048 entries for 32-bit -Kpic).
   Outside illumos, Jelinek's prelink draft (section 14, `LD_DEBUG=statistics`
   output for glibc on desktop C++ programs) reports relocation time as 98.3
   percent of dynamic-loader startup for konqueror with 79067 relocations
   (31169 relative) and no lookup cache; 96.5 percent for the same program with
   the lookup cache enabled; 97.2 percent for kmail. Those runs measure
   symbol-lookup-dominated loading of large C++ library sets on x86 and give
   neither a per-relocation cost nor an RP2040 estimate. The same document
   reports relocation at 2.2 percent for a prelinked run, which shows that
   resolving symbols ahead of time removes most of that share.
3. ld.so.1 self-relocation. OBS: `usr/src/cmd/sgs/rtld/amd64/_setup.c` loops
   over DT_RELACOUNT plus PLT entries and executes
   `*(ulong_t *)(r_offset + ld_base) += ld_base + r_addend`. The update adds to
   the word already stored at the target, so the preexisting content matters,
   and the comment says both RELATIVE and JMPSLOT entries remain to process.
   The loop never inspects `r_info`; that is sound only because ld.so.1 is
   linked with all global symbol references protected (symbolic), a contract
   the linker enforces for this one object. INF: the loop shows how small a
   RELATIVE-style fixup pass is, but it does not specify a loader for
   DiscoBSD. A fixed-window loader still needs a defined ARM relocation
   format (for example R_ARM_RELATIVE-class entries or a custom table), a
   symbol-binding model for references between the executable, libc text and
   per-process data, bounds and alignment validation of every target, and
   rejection of unsupported relocation types. `crle(1)` with dldump produces
   pre-relocated copies to cut startup cost.
4. Multicall. OBS: illumos ships one installed multicall wrapper. In
   `usr/src/cmd/ast/ksh/builtins`, `alias.c` resolves its executable path,
   takes the basename and looks it up in a fastpath table of builtins
   (`find_bfastpathrec`, then `fastpath_builtin_main`), falling back to
   `script_builtin_main`; the Makefile installs 28 names as hard links to the
   one `alias` binary in `/usr/bin` (bg, cd, cksum, cmp, comm, command, cut, fc,
   fg, getopts, hash, jobs, join, kill, paste, print, read, rev, sum, tee,
   test, type, ulimit, umask, unalias, uniq, wait, wc) and 17 XPG4 symlinks.
   I read the source and Makefile at commit `f0c8390b`; the package manifest
   `shell-ksh93.p5m` that the review cites for the shipped hard links was not
   opened. This is a basename-dispatched multicall in the busybox style,
   limited to ksh93's builtin set. Two other mechanisms stay separate.
   `isaexec` is one inode hard-linked under many names that re-execs the
   ISA-specific binary; it dispatches to a different program and shares no
   code. The ksh93 in-process builtins bound under `/usr/ast/bin` (integration
   commit for PSARC/2006/550 and 2007/035; current values not re-verified)
   form a third form, a multicall inside the shell that falls back to the
   external command. No crunched `/sbin` or miniroot multicall was found.
5. No-MMU. OBS: illumos targets no MMU-less device (Oxide RFD 291).

Bearing on DiscoBSD:
- For (INF): one stored copy of libc text removes the duplication Solaris
  names as a performance cost. Whether the stored copy is clean and complete
  for every command is not established by this stream; the relink evidence
  belongs to stream 01.
- Against (INF): the single fixed window removes the placement motive for PIC.
  ARMv6-M offers PC-relative literal loads and `ADR`-style address formation,
  so PIC needs no reserved base register by default. Measured on GCC 16.2.0
  with `-mcpu=cortex-m0plus -mthumb -Os`, reading one extern `int` compiles
  under `-fpic` to two literal loads, `add r3, pc`, a GOT load and the data
  load, using caller-clobbered registers and no `r9`; adding
  `-msingle-pic-base -mpic-register=r9` selects a dedicated base register and
  saves and restores `r9` around the access. The cost is the extra GOT load
  and the extra literal. Compare the default GOT model, single-base PIC and
  fixed-address code on the actual boxes before treating either as a tax.
- Against (INF): shipped commands upgrade atomically with the kernel, so the
  ABI-stability motive is reduced for them; persistent user-built binaries
  keep it (item 1).

## Resolution of the adversarial review

1. ACCEPT. `alias.c` in illumos-gate commit `f0c8390b` dispatches on the
   invocation basename through `find_bfastpathrec`; its Makefile hard-links 28
   names to one binary (names counted from the fetched Makefile). Body item 4
   now describes the installed multicall wrapper separately from the
   in-process builtins and `isaexec`. The package-manifest claim rests on the
   review and is marked unopened.
2. ACCEPT. I compiled the review's input with the local GCC 16.2.0
   (`arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -fpic -S`): output
   builds the GOT address with `ldr`, `add r3, pc`, and clobbered registers,
   with no `r9`; `-msingle-pic-base -mpic-register=r9` adds `mov`/`push`/`pop`
   around `r9`. The "base register" claim is withdrawn; the measured extra cost
   is one literal load and one GOT load per extern access.
3. ACCEPT. The fetched `_setup.c` loop is `*roffset += ld_base + r_addend`
   with a comment naming RELATIVE and JMPSLOT; the symbolic-link contract is
   what lets it skip `r_info`. Body item 3 now lists the format, binding,
   validation and rejection requirements a DiscoBSD loader needs.
4. Deferred to stream 01. I confirmed only that the cited script's input files
   (`elf32-arm-x.ld`, `libcx.syms.elf`) are absent from `measure/` and that
   `relink-results.txt` records three FAIL lines; no measure file was changed.
   The body no longer asserts a clean shared copy or a tiny loop.
5. ACCEPT. `sys/arch/rp2040/doc/STORAGE.md` ("What ships") records
   `/usr/bin/cc`, `smlrc`, `as` and `ld` linking against `/usr/lib/libc.a` on a
   writable root. Body items 1 and the closing list restrict the one-image
   argument to shipped commands and call for a user-binary compatibility rule.
6. ACCEPT. The Oracle "Move Read-Only Data to Text" page states that some PIC
   compilers place such assignments in writable `.picdata`. Body item 2 makes
   the text-relocation claim conditional on actual section placement.
7. ACCEPT. The illumos-gate `ld.1` text says merging is now the default and
   `-z combreloc` has no effect. Body item 2 labels the option historical.
8. ACCEPT. Extracting text from the Jelinek PDF shows 98.3 percent
   (konqueror, 79067 relocations, no cache), 96.5 percent (same program, cache
   on) and 97.2 percent (kmail), plus 2.2 percent for a prelinked run. Body
   item 2 states those conditions and disclaims any per-relocation or RP2040
   inference. I did not verify the review's "hot-cache" wording; the PDF text
   I read states the lookup-cache condition only.
