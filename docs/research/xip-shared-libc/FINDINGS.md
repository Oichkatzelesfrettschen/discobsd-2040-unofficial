# Findings ledger

Every finding raised by a research stream, its adversarial review, the naming
pass, or the advisory reconciliation, with its disposition and the artifact
or roadmap item that carries it, across nine streams. Review findings keep
their review numbering (`reviews/codex-NN-*.md`); each stream's "Resolution
of the adversarial review" section records the verification behind the
disposition. Roadmap items refer to the numbered list in `README.md`, section
"Roadmap".

The LARGE-epoch findings remain a historical ledger. The implemented fixed
192 KiB window and kernel-resident SwapRAM pool in
`../swap-exhaustion/review-04.md` supersede their live-layout remedy.

Dispositions: ACCEPT (the stream text now carries the correction), NARROWED
(accepted with a smaller scope than the review stated), RESOLVED-01 (a
measurement finding raised against another stream and repaired in stream 01).
No finding was rejected.

## Codex reviews

### 01 local measurement (9)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | `--just-symbols` silently bound sh's `rename(int,int)` to libc | ACCEPT | export-collision gate in `measure/relink_against_libcx.sh`, calibrated on sh and tee; roadmap 2 |
| 2 | major | library order excluded awk and login | ACCEPT | harness passes libraries after objects; both rows now pass |
| 3 | major | links are not runnable images | ACCEPT | stream 01 limits; stream 08 loader and swap analysis; roadmap 5-8 |
| 4 | major | native `cc` cannot call an XIP libc | ACCEPT | board ld range limit recorded; roadmap 9 |
| 5 | major | no PIC comparator was measured | ACCEPT | object-level r9 libc-data measurement in stream 08 |
| 6 | major | bundle could not replay its derivation | ACCEPT | harness generates its linker script, symbol file and export list |
| 7 | major | map attribution taken as ownership | ACCEPT | figures labeled linker attribution; savings use whole-image blocks |
| 8 | minor | flash cost understated | ACCEPT, corrected value | `flashimg -c`: 28 KB for a 48 KB partition |
| 9 | minor | box saving stated with the wrong sign | ACCEPT | boxes save 44 blocks |

### 02 MINIX (8)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | the wiki's 10x figure is not a libc-sharing measure | ACCEPT | stream 02 body |
| 2 | major | MINIX does not show relocation needs an MMU | ACCEPT | stream 02 body |
| 3 | major | "missing library is fatal" is a file dependency, not a flash property | ACCEPT | stream 02 body |
| 4 | major | MINIX carries NetBSD crunchgen infrastructure | ACCEPT | stream 02 item 4 |
| 5 | major | prototype rows not replayable | RESOLVED-01 | harness |
| 6 | major | "static PIE unsupported" overstated | ACCEPT | stream 02 item 3 |
| 7 | minor | base Makefiles default, not force, `-static` | ACCEPT | stream 02 item 1 |
| 8 | minor | MINIX 1/2 shared-text chronology | ACCEPT | stream 02 item 5 |

### 03 illumos (8)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | major | illumos ships an `alias` multicall with 28 links | ACCEPT | stream 03 item 4 |
| 2 | major | default `-fpic` needs no reserved base register | ACCEPT | stream 03 bearing section |
| 3 | major | ld.so.1's loop adds to the stored word under a symbolic-link contract | ACCEPT | stream 03 item 3 |
| 4 | major | relink evidence incomplete | RESOLVED-01 | harness gates |
| 5 | major | ABI motive reduced, not absent, because user-built binaries persist | ACCEPT | versioned libc header; roadmap 6 |
| 6 | minor | text-relocation example is toolchain-conditional | ACCEPT | stream 03 item 2 |
| 7 | minor | `-z combreloc` is historical syntax | ACCEPT | stream 03 item 2 |
| 8 | minor | prelink percentages need their conditions | ACCEPT | stream 03 item 2 |

### 04 BSD source trees (5)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | major | ordinary PIC is available on Thumb-1; only FDPIC is refused | ACCEPT | stream 04 body |
| 2 | major | a RELATIVE-only loop assumes one base for target and value | ACCEPT | stream 04 body; stream 08 design B relocates GOT entries by segment |
| 3 | minor | OpenBSD's ARM startup restriction is the stack protector only | ACCEPT | stream 04 body |
| 4 | minor | retrobsd builds 155 command targets, not about 100 | ACCEPT | stream 04 body |
| 5 | minor | path prefixes and the 211bsd magic range | ACCEPT | stream 04 body |

### 05 no-MMU prior art (9)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | the deciding experiment lacked elapsed time, storage and controls | ACCEPT | stream 05 deciding measurement; roadmap 8 |
| 2 | major | compiler percentages lacked a replay recipe | ACCEPT | stream 05 item 3 (commit, flags, failures, denominators) |
| 3 | major | Tock fixes up in application startup with flash text | ACCEPT | stream 05 item 4; adopted as design B |
| 4 | major | only alias 0x13 bypasses the XIP cache | ACCEPT | stream 05 item 5 |
| 5 | major | bFLT flags request, not guarantee, XIP | ACCEPT | stream 05 item 1 |
| 6 | major | "every file" exceeds the relinked set | ACCEPT | stream 05 FOR/AGAINST |
| 7 | minor | Clang ROPI accepts address-initialized C globals | ACCEPT | stream 05 item 3 |
| 8 | minor | NXFLAT's current register is r9 | ACCEPT | streams 05 and 06 |
| 9 | minor | four bFLT slots are the executable plus three libraries | ACCEPT | stream 05 item 1 |

### 06 RTOS and microkernels (10)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | LLEXT has four loader behaviors; relocation was dismissed unmeasured | ACCEPT | stream 06 LLEXT row |
| 2 | blocker | flash-resident user data fails the window-only pointer check | ACCEPT | libc flash admission; roadmap 3 |
| 3 | major | lazy libc state needs an ABI first | ACCEPT | roadmap 4 |
| 4 | major | the XIP-program falsifier was invalid | ACCEPT | stream 06 technique 3 |
| 5 | major | per-image heatshrink resets cannot test cross-image pooling | ACCEPT | stream 06 technique 6 |
| 6 | major | SVC entry gives no immunity; fuzzing is not proof | ACCEPT | stream 06 technique 7; open audit item |
| 7 | major | NXFLAT uses r9 | ACCEPT | stream 06 NXFLAT row |
| 8 | minor | CROMFS is memory-addressed data, not instruction XIP | ACCEPT | stream 06 CROMFS row |
| 9 | minor | the u-area already swaps with the process | ACCEPT | stream 06 technique 8 |
| 10 | major | provenance not replayable | ACCEPT | pinned commits; search-summary rows marked |

### 07 RP2040 hardware (11)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | LARGE-epoch stack lies outside the MPU map | ACCEPT | `MPU.md` known-gap section; roadmap 1 |
| 2 | major | the falsifier measured file size, not resident demand | ACCEPT | stream 07 falsifier |
| 3 | minor | the first denied access depends on argument layout | ACCEPT | stream 07 defect section |
| 4 | minor | retained LARGE runs predate the MPU | ACCEPT | stream 07 defect section |
| 5 | major | XN is unsupported by the current map, not impossible | ACCEPT | stream 07 inventory |
| 6 | major | user stack guard costs 32-63 bytes | ACCEPT | stream 07 inventory; roadmap 11 |
| 7 | major | DMA payoff unmeasured | ACCEPT | stream 07 inventory |
| 8 | major | 20-40x is a per-miss transfer estimate | ACCEPT | stream 07 inventory |
| 9 | minor | WFE has single-core uses | ACCEPT | stream 07 inventory |
| 10 | minor | 220 bytes are untouched sentinels, not a margin | ACCEPT | stream 07 inventory; roadmap 10 |
| 11 | minor | SDK pin lines and ISA-absence citations | ACCEPT | stream 07 header |

### 08 XIP libc design (7)

| # | Severity | Finding | Disposition | Carried by |
| --- | --- | --- | --- | --- |
| 1 | blocker | PIC alternative omitted | ACCEPT | design A/B comparison; design B recommended |
| 2 | blocker | process-length invariant incomplete for fixed data | NARROWED | both lengths become `L + text + data + bss` in design A; design B avoids it |
| 3 | blocker | packed loader keeps the old base | ACCEPT | design A changes both loaders; design B avoids it |
| 4 | blocker | `brk()` still clamps against `_end` | ACCEPT | two libc-owned break cells; roadmap 4 |
| 5 | major | N_ABS storage is not a callable import | ACCEPT | roadmap 9 |
| 6 | major | 187-block figure unstable | ACCEPT | frozen rows 502 to 288 and 475 to 278 |
| 7 | minor | Dhara estimate high | ACCEPT | 28 KB by `flashimg -c` |

## Stream findings outside the reviews

| Source | Finding | Disposition | Carried by |
| --- | --- | --- | --- |
| stream 07 | MPU.md's Renode transcript reads `programmed=3` while the board and code say 4 | open; a retained transcript stays as captured | `MPU.md` known-gap section notes it; regenerate on the next Renode gate run |
| stream 07 | `machdep.mpu.enable` reports cached state, not a live readback | adopted | LARGE-gap falsifier; roadmap 1 |
| stream 07 | SYSRESETREQ leaves DMA and PIO state running across a kernel reboot | recorded | stream 07; relevant only when the kernel drives DMA |
| stream 07 | panic PC in watchdog scratch3; XIP and BUSCTRL counters; non-allocating alias for data reads; ROM memcpy benchmark | adopted as measured candidates | roadmap 10-12 |
| stream 07 | XIP cache as SRAM, core1, DMA, interpolators | not pursued without measurement | README "Clever savings" |
| stream 06 | versioned header at the libc base (a.out DLL, NuttX userspace header) | adopted | roadmap 6 |
| stream 06 | MPU subregion-edge audit (TickTock bug class) | adopted | roadmap 1 gate and mputest edge probes |
| stream 06 | per-program stack budgets from `-fstack-usage` | candidate | roadmap 12 |
| stream 08 | libgcc leaf helpers stay private to each program | adopted | roadmap 6 member policy |
| stream 08 | weak `_doprnt` hooks resolve inside libc; ROM float trampolines use absolute cell addresses | open decision | roadmap 5 (rewrite r9-relative or keep float printf out of libcx) |
| stream 08 | veneer count reported as a gate figure | adopted | roadmap 7 |
| stream 08 | crt0 must exit cleanly when libc data does not fit; a gate checks image plus libc data plus stack against MAXMEM | adopted | roadmap 5 |
| stream 08 | make `_ctype_` const | landed in `6b76f490`; the table sits in `a_text`, which swap skips, and still occupies the window, since text and data load as one extent | README "Static libc state under static links"; roadmap 4 |
| stream 01, 08 | allocate `gr_mem`, crypt `KS`, regex `expbuf` and the 256-byte line buffers on first use | NARROWED under static links: getgrent's 1,057-byte record is allocated on first use, so box, find, login, su, tar and utilbox lose 1,056 bytes of bss and one flash swap block for 24 bytes of text; `KS`, `expbuf` and the line buffers stay static, saving no kilobyte or swap block outside main-path callers at this layout | README "Static libc state under static links"; `check-libc-getgrent`; roadmap 4 |

## Stream 09: historical UNIX archive

| # | Finding | Disposition | Carried by |
| --- | --- | --- | --- |
| 1 | SVR3 `libc_s` spec: fixed text and data addresses, numbered append-only branch table with reserved ranges, hide-by-default exports | adopted | README "Precedent"; roadmap 6 |
| 2 | `mkshlib` host stub archive attaches a dependency record (pathname, no ABI version) through an undefined-symbol reference | adopted; the RP2040 record adds an ABI major, slot-count floor and state-layout contract | roadmap 6 and 9 |
| 3 | SVR3 pointer-indirects program-owned data; its fixed data address is an MMU-backed implementation, not a necessity | adopted; design B chosen on loader and swap scope, design A kept as the measured fallback | README "Precedent"; roadmap 4, 5, 8 |
| 4 | SVR3 exec validates the library record, caps libraries (`SHLBMAX = 2`), names ELIB errors; `chkshlib` checks compatibility offline | adopted | roadmap 6 (header, hash, `chkshlib`-style gate) |
| 5 | OSF/1 pre-relocated fixed libraries, `so_locations`, `mvalid` | adopted (`mvalid` model) | roadmap 3 |
| 6 | SunOS 4 major/minor rule; crt0 relocates only when needed | adopted | roadmap 6 header fields |
| 7 | Palm OS per-library globals pointer beside the dispatch table (allocation timing undocumented); a reused trap number | open-call allocation adopted as a proposed convention; renumbering forbidden | roadmap 4 and 6 |
| 8 | 2.11BSD `ld -Z` 8-byte thunks; PC-Xinu stack canary at switch | adopted | roadmap 9 and 11 |
| - | Lites keeps referenced library bytes immutable and renames a tested replacement into place | adopted as the immutability rule; whole-image reflash is the RP2040 policy | README roadmap closing rule |
| - | L4Ka RC2, OKL4 3.0 (with a 1,350-entry Pistachio snapshot in its test tree), seL4 10.1.1: MMU-only, nothing transferable beyond pointer-indirected imports | recorded | stream 09 |
| - | audit claim that ARMv6-M lacks MPU subregions | rejected | PMSAv6 subregion-disable bits, used in `mpu.c` |
| - | `SHLBMAX` path missing `.unpacked` | corrected | stream 09 item 4 |
| - | `rom_float_resolver.S` already binds user code to fixed-address ROM code with a version floor and a window-bounded cache cell | adopted as the in-tree precedent | stream 09 addendum; roadmap 5-6 |
| - | the LARGE repair and a break-following stack guard break MPU.md's programmed-once invariant | adopted | roadmap 1 and 11 restate the invariant |
| - | variable shifts in span and MPU-size arithmetic | audit against the ARMv6-M pseudocode; the universal large-count claim withdrawn | roadmap 1, 3, 11 |

### Review of stream 09 (13)

All thirteen ACCEPT (finding 8 extended, finding 10 accepted with the shift
claim withdrawn); each is listed in the stream's resolution section and
reflected in the rows above.

## Naming

| Source | Finding | Disposition |
| --- | --- | --- |
| Grok persona | `box` ranks above `crunch`, `crunchbox`, `crunchpod` | adopted, on reduced grounds |
| Grok persona | long names cost `ls` columns on the 53-column console | rejected: `ls` rounds to tab stops (`bin/ls/ls.c:443-444`), so 8 to 15 characters share one 16-column slot |
| Grok persona | `crunch` becomes inaccurate once libc leaves the file | rejected: crunchgen's defining property is merging programs with argv[0] dispatch |
| Grok persona | `crunchbox` collides with Crunch.io crunchBox and CrunchBang tooling | accepted |

## Advisory reconciliation

| Finding | Evidence | Carried by |
| --- | --- | --- |
| The "331 KB" libc figure is the archive file size; the whole libc is 50,000 bytes of text | `arm-none-eabi-size -t lib/libc.a`; `--whole-archive` link | stream 01 |
| Whole-libc and union images save the same blocks per program; the union costs 11 fewer partition blocks and three fewer collisions | relink results; `flashimg -c` | README decisions |
| Placing `--just-symbols` before crt0 removes the exit, _exit and fakcu residue | residual-member rule | harness |
| r9-relative libc data costs +7.2 % text and about 696 bytes of GOT at object level | 185 libc sources compiled both ways | stream 08 comparison |
| The LARGE gap is confirmed by the retained board transcript of the 0x20024000 probe | `MPU.md` board transcript | `MPU.md` permanent-window section |
| The kernel measures 108,726 bytes against a 130,816-byte region; the `flash.h` comment's 90 KB figure is stale | `arm-none-eabi-size`; `RP2040.ld:23` | `flash.h` comment, `STORAGE.md` |
| Codex reviewers ran with write access to the source tree; status before and after shows no change in the checkout or the BSD trees | `git status --porcelain` snapshots | this ledger |
