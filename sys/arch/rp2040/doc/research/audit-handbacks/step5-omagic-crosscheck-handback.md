# OMAGIC candidate cross-check

## Selected-program derivation

The supplied 25-program JSON is the finite selection authority. The parent audit confirms that its programs belong to the manifest-relevant standalone set. Sorting `programs` by descending `unmapped_candidate_bytes` selects awk, fsck, find, getty, and re. The cross-check makes claims only about their retained candidates. Each selected program has zero `discardable_archive_bytes` in the supplied filter output.

| Program | Raw candidate bytes | Libgcc co-retained input bytes | Unmapped bytes | Binary bytes | `.word` bytes inside unmapped spans | Remaining unknown bytes |
|---|---:|---:|---:|---:|---:|---:|
| awk | 1990 | 76 | 1914 | 61648 | 272 | 1642 |
| fsck | 1418 | 420 | 998 | 30884 | 84 | 914 |
| find | 1202 | 420 | 782 | 19784 | 56 | 726 |
| getty | 1144 | 420 | 724 | 19548 | 92 | 632 |
| re | 870 | 192 | 678 | 33246 | 60 | 618 |
| Total | 6624 | 1528 | 5096 | 165110 | 564 | 4532 |

The independently admitted static discardability ceiling is **0 bytes**. The 5096-byte candidate ceiling includes 564 data-span bytes and 4532 unknown instruction/padding bytes. Literal-pool classification establishes data ownership, rather than recoverable storage.

## Tool/version table

| Tool / command path | Package provenance | Observed version / limit |
|---|---|---|
| `/usr/bin/jq` | jq 1.8.2-1.1 | jq-1.8.2 |
| `/usr/bin/global` | global 6.6.15-1.1 | GNU Global 6.6.15; read-only existing DB |
| `/usr/bin/cscope` | cscope 15.9-3.2 | 15.9; `-d -L` existing DB |
| `/usr/bin/cflow` | cflow 1.8-1 | 1.8; raw K&R parsing; `_findiop` was missed |
| `/usr/bin/arm-none-eabi-objdump`, `arm-none-eabi-nm` | arm-none-eabi-binutils 2.47-1.1 | objdump 2.47.20260726; nm rejects OMAGIC |
| `/usr/local/bin/python3` -> `/usr/bin/python3.14` | launcher unowned; `/usr/bin/python3` owned by python 3.14.7-2 | Python 3.14.7; invoked through `PYTHON_INTERPRETER` with `-B` |
| `/usr/lib/python3.14/site-packages/capstone/__init__.py` | python-capstone 5.0.9-1.1 | module reports 5.0.7; discrepancy retained |
| `/usr/lib/python3.14/site-packages/lief/__init__.py` | python-lief 0.17.3-1 | module reports 0.17.0-; imported for provenance, unused for a.out parsing |
| `/usr/bin/ghidra-analyzeHeadless` -> `/opt/ghidra/support/analyzeHeadless` | ghidra 12.1.2-2.1 | raw ARM:LE:32:Cortex import; OpenJDK 26.0.2; reflective/Unsafe warnings |

## Candidate reconciliation

Program abbreviations: A=awk, F=fsck, N=find, G=getty, R=re. Each byte denotes a program occurrence. All rows have analyzer classification `unmapped_candidates`. `D/U` partitions `.word` data from unknown instruction/padding bytes. U admission is `unknown`; D admission is `reject-as-function-instruction-count`.

Evidence markers: `B` means the independent Capstone numeric-branch pass found the row disconnected from the supplied entry/stored-pointer/linker roots, and every parsed retained instruction/data encoding matched the a.out payload. `G0` means Ghidra returned `FUNCTION null` at the queried entry, so decompilation corroboration is unavailable. `GX` means that candidate was outside the seven Ghidra entry queries. `I` means GNU Global definition output; `C` means canonical-path cscope query output; `L` means cflow corroboration. Source references below are relative to `<repository root>`. Source/index observations remain subject to the HEAD discrepancy recorded below.

Falsifiers: `F1` = section co-retention or a resolved incoming indirect edge; `F2` = macro/generated-source provenance plus target resolution; `F3` = mapping/reference evidence contradicting the `.word` split. Positive admission requires independent-section proof and complete incoming-edge accounting.

| Program / symbol(bytes) | Total; D/U | Source and index / cflow evidence | objdump / Ghidra evidence | Decision / falsifier |
|---|---:|---|---|---|
| A xatan(260) | 260; 72/188 | I,C,L `lib/libm/atan.c:36`; satan calls xatan | B; raw objdump agrees through return at 0x20004fc0; literals 0x20004fc4..0x2000500b; G0 | unknown / F1,F3 |
| A atan2(212) | 212; 16/196 | I,L `lib/libm/atan.c:84`; atan2 -> satan -> xatan | B; G0 | unknown / F1 |
| A asin(204), acos(92) | 296; 44/252 | I `lib/libm/asin.c:15,41`; acos -> asin -> atan | B; G0 at both entries | unknown / F1 |
| A satan(160), atan(44) | 204; 32/172 | I,C,L `lib/libm/atan.c:53,69`; internal math chain | B; G0 at both entries | unknown / F1 |
| A log10(24), ceil(20) | 44; 8/36 | I `lib/libm/log.c:56`, `lib/libm/floor.c:25`; definitions indexed only | B; GX | unknown / F1 |
| A input(88), yy_scan_buffer(84), yy_switch_to_buffer(72), yy_scan_bytes(72) | 316; 28/288 | Exact yy_scan_buffer Global query yields zero definitions; generated lexer body unavailable through permitted index | B; scan_bytes -> scan_buffer -> switch; G0 for scan_buffer, GX others | unknown / F1,F2 |
| A yypush_buffer_state(68), yylex_destroy(68), yypop_buffer_state(60), yy_delete_buffer(56), yy_scan_string(18) | 270; 16/254 | Generated lexer source-index gap | B; destroy -> pop/delete; scan_string -> scan_bytes; GX | unknown / F1,F2 |
| A yyget_lineno(10); yyget_in/out/leng/text, yyset_lineno/in/out, yyget_debug, yyset_debug (12 each); yyalloc/realloc/free (8 each) | 142; 40/102 | Generated lexer source-index gap; names are exact JSON suffix expansions | B; GX | unknown / F1,F2 |
| A scanf(28), fscanf(22) | 50; 4/46 | I `lib/libc/stdio/scanf.c:5,17`; both call _doscan; sscanf shares source | B; varargs bx-r3 return paths require interpretation; GX | unknown / F1 |
| A PUTS(2) | 2; 0/2 | I `usr.bin/awk/lib.c:225`, macro in `usr.bin/awk/awk.g.y:38`; definition output only | B; GX | unknown / F1,F2 |
| R win_remove(244) | 244; 16/228 | I `usr.bin/re/r.window.c:256`; caller `r.cmd.c:700` guarded by MULTIWIN | B; GX; interior-address coincidences remain unclassified | unknown / F1,F2 |
| R win_open(196) | 196; 24/172 | I,C `usr.bin/re/r.window.c:176`; `r.cmd.c:469,705` guarded by MULTIWIN | B; GX; compiled call sites absent from parsed .dis | unknown / F1,F2 |
| R freesegm(18) | 18; 0/18 | I `usr.bin/re/r.edit.c:1269`; source definition | B; GX | unknown / F1 |
| N getunum(220) | 220; 4/216 | I,L `usr.bin/find/find.c:661`; cscope canonical callers empty; cflow sees fopen/getc/atoi/fclose | B; GX; one unaligned interior-address coincidence | unknown / F1 |
| F unrawname(64), rawname(60), blockcheck(2) | 126; 8/118 | I `sbin/fsck/main.c:30,49,66`; blockcheck returns at line 69 before lexical rawname/unrawname references | B; GX; blockcheck compiled to 2 bytes | unknown / F1 |
| G _findiop(112), _f_morefiles(72) | 184; 24/160 | I,C `lib/libc/stdio/findiop.c:60,29`; canonical fopen/fdopen callers belong to library source; cflow misses FILE* K&R definition | B; _findiop -> _f_morefiles -> calloc/getdtablesize; GX | unknown / F1 |
| G asctime(80), ctime(12) | 92; 20/72 | I `lib/libc/gen/ctime.c:28,17`; definitions indexed only | B; ctime -> asctime; GX | unknown / F1 |
| G calloc(24), bzero(20), getdtablesize(20) | 64; 4/60 | I `lib/libc/gen/calloc.c:9`, `bzero.c:15`; getdtablesize user syscall stub lacks a Global definition | B; calloc -> bzero; getdtablesize called by _f_morefiles; GX | unknown / F1 |
| G setlogmask(16) | 16; 4/12 | I `lib/libc/gen/syslog.c:222`; definition indexed only | B; GX | unknown / F1 |
| F getpwnam(64) | 64; 4/60 | I `lib/libc/gen/getpwent.c:136`; definition indexed only | B; GX | unknown / F1 |
| F getfsspec(48), getfsfile(48) | 96; 8/88 | I `lib/libc/gen/fstab.c:156,167`; definitions indexed only | B; GX | unknown / F1 |
| F verr(72), verrx(46), vwarnx(38), warnx(24), err(22), errx(22) | 224; 16/208 | I `lib/libc/gen/err.c:66,105,174,186,86,119`; definitions indexed only | B; err -> verr, errx -> verrx, warnx -> vwarnx; GX | unknown / F1 |
| F strncpy(32), rindex(24) | 56; 0/56 | I `lib/libc/gen/strncpy.c:8`, `rindex.c:9`; definitions indexed only | B; timezone -> strncpy; rawname/unrawname -> rindex; GX | unknown / F1 |
| N sigvec(40), sigpause(12), sigaction(20), sigsuspend(20) | 92; 4/88 | I `lib/libc/compat/sigcompat.c:37,78`; `lib/libc/arm/sys/sigaction.S:22` passes sigtramp to kernel; sigsuspend user stub lacks Global definition | B; sigvec -> sigaction; sigpause -> sigsuspend; GX | unknown / F1 |
| N index(16) | 16; 0/16 | I `lib/libc/gen/index.c:9`; definition indexed only | B; timezone -> index; GX | unknown / F1 |
| F,N,G,R realloc(104 each) | 416; 16/400 | I `lib/libc/gen/malloc.c:185`; definition indexed only | B; GX | unknown / F1 |
| A,F,N,G,R atexit(56,54,56,56,52) | 274; 40/234 | I `lib/libc/stdio/exit.c:30`; exit line 21 dispatches stored callbacks; shared source suggests retention pressure, not section proof | B; GX | unknown / F1 |
| A,F,N,G f_prealloc(52 each) | 208; 16/192 | I `lib/libc/stdio/findiop.c:89`; cflow sees calloc but misses _f_morefiles; direct decoder recovers dependency | B; GX | unknown / F1 |
| F,N,G,R brk(36 each) | 144; 32/112 | I `lib/libc/arm/sys/sbrk.c:30`; sbrk at line 12 and brk both call _brk | B; GX; brk and _brk are distinct symbols | unknown / F1 |
| F,N,G timezone(84 each) | 252; 24/228 | I `lib/libc/gen/timezone.c:25`; definition indexed only | B; GX | unknown / F1 |
| F,N,G gmtime(28 each) | 84; 24/60 | I `lib/libc/gen/ctime.c:312`; definition indexed only | B; GX | unknown / F1 |
| N,R execlp(20 each) | 40; 0/40 | I `lib/libc/gen/execvp.c:14`; definition indexed only | B; bx-r3 varargs return idiom; GX | unknown / F1 |
| F,N getpwent(44 each), setpwfile(12 each), setpwent(10 each) | 132; 16/116 | I `lib/libc/gen/getpwent.c:122,205,180`; definitions indexed only | B; GX | unknown / F1 |
| A,F,N,G,R cfree(10,8,8,8,8) | 42; 0/42 | I `lib/libc/gen/calloc.c:22`; definition indexed only | B; GX | unknown / F1 |

The next table partitions the separate 1528-byte libgcc denominator. Its archive-section classifications are inherited from the hashed supplied filter output, rather than a fresh archive read. The allowed binary cross-check independently overturns one disconnected-function classification.

| Program / candidates | Bytes | Independent evidence | Final category / falsifier |
|---|---:|---|---|
| R __udivsi3 | 120 | `re.dis:14033,14035`: reachable __aeabi_uidivmod branches/calls 0x200075e2 / 0x20007578 inside __udivsi3 [0x20007574,0x200075ec) | reachable/false-positive; reject discardability; falsify by correcting numeric target/boundary or root provenance |
| R __aeabi_cdrcmple(16), __aeabi_cdcmpeq(16), __aeabi_dcmple(20), __aeabi_dcmpge(20) | 72 | Filter maps to _arm_cmpdf2.o:.text; .dis corroborates emitted labels | archive-co-retained, inherited; F1 |
| A __aeabi_fcmpge(20), __aeabi_cdrcmple(16), __aeabi_cdcmpeq(16), __aeabi_cfcmpeq(16), __aeabi_cfrcmple(8) | 76 | Filter maps _arm_cmpsf2.o/.text and _arm_cmpdf2.o/.text; .dis corroborates labels | archive-co-retained, inherited; F1 |
| F,N,G each: __gedf2(200), __eqdf2(116), __aeabi_dcmple/dcmpgt/dcmpge(20 each), __aeabi_cdrcmple/cdcmpeq(16 each), __aeabi_dcmpeq(12) | 1260 | Filter includes gedf2.o:.text.__gedf2, eqdf2.o:.text.__eqdf2 and _arm_cmpdf2.o:.text | archive-co-retained, inherited; F1 |

Thus the closed 6624-byte classification is: 0 independently discardable + 1408 archive-co-retained (inherited) + 120 reachable/false-positive + 564 data-span artifacts + 4532 unknown. Archive retention and reachability overlap conceptually; the table assigns the 120-byte finding to the stronger reachable category once.

## Disagreements and limits

1. OMAGIC headers are eight little-endian words, magic 263, payload offset 32, text base 0x20000000. All five report zero relocation and symbol-table byte sizes. The independent pass matched 44952/26348/17662/15760/30240 encoded bytes for A/F/N/G/R, with zero mismatched encoded lines. The pass covers parsed `.dis` encodings, not the entire text image: raw decoder coverage remains bounded by retained code/data annotations.
2. The analyzer resolves branch targets by normalized label name. `.udivsi3_skip_div0_test` is an interior assembly label excluded from function names. Numeric containment recovers the re call into __udivsi3. The branch mnemonic predicate also omits common conditional spellings such as `beq.n` and `bne.n`; adding numeric conditional branches admits zero additional unmapped candidates in these five binaries.
3. Raw objdump turns xatan's constant pool into plausible Thumb instructions. The annotated `.dis` identifies 72 bytes as `.word`, and xatan loads those constants before returning. The 564-byte total is the exact sum of `.word` records inside unmapped candidate ranges. Nops/padding remain in unknown bytes.
4. The expanded pass inherits the supplied root set and detects 14/17/15/8/6 register branch/call sites for A/F/N/G/R. Several bx-r3 sites implement varargs returns, while execute, fsck callback scans, find predicate dispatch, exit callbacks, and _fwalk need pointer target reasoning. Interior-address word coincidences occur, including unaligned words in code; they are neither proven pointers nor grounds for removal. Fallthrough diagnostics also encounter padding after returns; those diagnostics are not admitted edges.
5. Global/cscope are lexical. The fsck source has lexical calls after an unconditional return; re has MULTIWIN guards; generated awk lexer definitions are absent from the queried index. Cflow's raw K&R parsing sees the atan2 math chain and getunum calls but misses FILE* _findiop and some dependencies. None of those lexical results proves execution or section discardability.
6. `rg --files` for `*.map`, `*.sym`, `*.symbols` within the five selected program directories returned zero paths. nm rejected awk's OMAGIC format. ELF intermediates and archive objects were outside the selected-binary allowance. Whole source-file co-location is retention evidence to investigate, rather than proof of a shared allocated input section.
7. Ghidra imported exactly one binary, awk, 61648 bytes, with raw payload offset 32, requested base 0x20000000 and length 61616. The importer loaded zero additional files. Analysis succeeded. A subsequent read-only project query returned `FUNCTION null` and zero entry references at xatan, atan2, asin, satan, acos, atan, and yy_scan_buffer. The successful import supplies zero candidate decompilations. Explicit function/Thumb seeding and import-layout verification would be required for stronger Ghidra evidence; further Ghidra invocations stopped after the parent's instruction.
8. Delegated HEAD was ccfa7882e75414e2b28baa84c21b74f90b55d1a9. Explicit concluding `git -C` observed **386b8f7239e02b5f6901583cbc69d648c91138d2**. The parent confirmed clean main and one intervening bubble selftest commit. Source hashes bind the observed files; index freshness remains unverified. Canonical checkout paths alone were admitted; historical scratch-tree hits were rejected. The subagent performed zero source edits/rebuilds/relinks.

## Boundary observations

Unique non-Ghidra evidence payload: **3053764 bytes across 27 files**, consisting of two JSONs, two analyzers, five a.out/.dis pairs, and thirteen selected source files. The 24 MiB ceiling excludes requested prebuilt index infrastructure per explicit parent clarification. Database sizes: cscope.out 81782163; GPATH 1400832; GRTAGS 14639104; GTAGS 19406848 bytes. Database hashes and freshness proofs were unavailable; the parent directed retention of size/provenance rather than a new index hash pass.

The Ghidra launcher reported cache maintenance and a packed database cache outside the requested scratch directory. These ancillary writes violate the intended write boundary. Read-only enumeration observed the following files during the run; before-run existence/hashes are unknown. The subagent preserved these files and stopped further Ghidra launches.

| Ancillary path | Observed bytes | mtime, America/Los_Angeles |
|---|---:|---|
| /var/tmp/eirikr-ghidra/fscache2/.lastmaint | 46 | 2026-09-12 13:18:21.414775203 -0700 |
| /var/tmp/eirikr-ghidra/packed-db-cache/cache.map | 126 | 2026-09-12 13:18:28.405268870 -0700 |
| /var/tmp/eirikr-ghidra/packed-db-cache/pdb90255438/db.1.gbf | 7012352 | 2026-09-12 13:18:28.399268841 -0700 |

Directory observations: `/var/tmp/eirikr-ghidra` mtime 13:18:28.349268605; `fscache2` 13:18:21.414235696; `packed-db-cache` 13:18:28.406068177; `packed-db-cache/pdb90255438` 13:18:28.370325413, all on 2026-09-12 -0700, each stat size 4096. Ghidra also read existing preferences under `<the user's Ghidra preferences directory>/`; changes to that configuration surface were unmeasured. No broad filesystem-preservation claim follows from these observations.

## Exact commands and input hashes

Read commands used stdout; report/script creation used apply_patch. One mkdir/import exec failed before process creation because its working directory was absent; separate mkdir and import succeeded.

```sh
REPO=<repository root>
SCRATCH=<audit scratch directory>
GTAGSROOT=$REPO GTAGSDBPATH=$SCRATCH/global-db global -x '^(xatan|satan|atan|atan2|asin|acos|ceil|log10|win_remove|win_open|freesegm|getunum|unrawname|rawname|blockcheck|_findiop|f_prealloc|realloc|timezone|yy_scan_buffer|input|PUTS)$'
GTAGSROOT=$REPO GTAGSDBPATH=$SCRATCH/global-db global -rx '^(xatan|satan|atan2|win_remove|win_open|freesegm|getunum|unrawname|rawname|blockcheck|yy_scan_buffer)$'
GTAGSROOT=$REPO GTAGSDBPATH=$SCRATCH/global-db global -x '^(atexit|_f_morefiles|getdtablesize|asctime|ctime|gmtime|calloc|cfree|bzero|setlogmask|scanf|fscanf|execlp|brk|sigvec|sigpause|sigaction|sigsuspend|getpwnam|getpwent|setpwent|setpwfile|getfsspec|getfsfile|strncpy|rindex|index|verr|verrx|vwarnx|err|errx|warnx)$'
jq '.programs|sort_by(-.unmapped_candidate_bytes)|.[0:5]' "$SCRATCH/aout-reachability-25-libgcc-filtered.json"
cscope -d -L -f "$SCRATCH/cscope.out" -1 xatan
cscope -d -L -f "$SCRATCH/cscope.out" -3 xatan
cscope -d -L -f "$SCRATCH/cscope.out" -3 win_open | /usr/bin/rg '^'
cscope -d -L -f "$SCRATCH/cscope.out" -3 getunum | /usr/bin/rg '^'
cscope -d -L -f "$SCRATCH/cscope.out" -3 _findiop | /usr/bin/rg '^'
cd "$REPO"
cflow --no-preprocess --depth=4 --main=atan2 lib/libm/atan.c lib/libm/asin.c
cflow --no-preprocess --depth=3 --main=_findiop lib/libc/stdio/findiop.c
cflow --no-preprocess --all --depth=3 --main=_findiop lib/libc/stdio/findiop.c
cflow --no-preprocess --depth=2 --main=getunum usr.bin/find/find.c
/usr/bin/rg -n -C 5 'win_open|win_remove|MULTIWIN|freesegm' usr.bin/re/r.cmd.c usr.bin/re/r.edit.c usr.bin/re/r.defs.h
/usr/bin/rg -n -C 4 'getunum|blockcheck|rawname|unrawname|#if|#else|#endif' usr.bin/find/find.c sbin/fsck/main.c
/usr/bin/rg -n -C 3 '\.udivsi3_skip_div0_test' usr.bin/re/re.dis
/usr/bin/rg --files -g '*.map' -g '*.sym' -g '*.symbols' usr.bin/awk sbin/fsck usr.bin/find libexec/getty usr.bin/re
arm-none-eabi-nm -an usr.bin/awk/awk
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x1fffffe0 --start-address=0x20004f08 --stop-address=0x2000500c usr.bin/awk/awk
git -C "$REPO" -c core.fsmonitor=false rev-parse --show-toplevel --git-dir HEAD
pacman -Q jq global cscope cflow arm-none-eabi-binutils ghidra python python-capstone python-lief
pacman -Qo /usr/bin/jq /usr/bin/global /usr/bin/cscope /usr/bin/cflow /usr/bin/arm-none-eabi-objdump /usr/bin/ghidra-analyzeHeadless
readlink -f /usr/bin/ghidra-analyzeHeadless
readlink -f /usr/local/bin/python3
```

Ghidra invocations (second invocation was already running when the parent stop arrived):

```sh
mkdir -p <audit scratch directory>/ghidra-omagic-crosscheck
cd <audit scratch directory>/ghidra-omagic-crosscheck
/usr/bin/ghidra-analyzeHeadless <audit scratch directory>/ghidra-omagic-crosscheck awk-omagic -import usr.bin/awk/awk -processor ARM:LE:32:Cortex -loader BinaryLoader -loader-baseAddr 0x20000000 -loader-fileOffset 32 -loader-length 61616 -analysisTimeoutPerFile 60 -max-cpu 2 -log <audit scratch directory>/ghidra-omagic-crosscheck/import.log -scriptlog <audit scratch directory>/ghidra-omagic-crosscheck/script.log
/usr/bin/ghidra-analyzeHeadless <audit scratch directory>/ghidra-omagic-crosscheck awk-omagic -process awk -noanalysis -readOnly -scriptPath <audit scratch directory>/ghidra-omagic-crosscheck -postScript CandidateEvidence.java -max-cpu 2 -log <audit scratch directory>/ghidra-omagic-crosscheck/decompile.log -scriptlog <audit scratch directory>/ghidra-omagic-crosscheck/decompile-script.log
/usr/bin/rg --files --hidden /var/tmp/eirikr-ghidra
stat -c '%s %y %n' /var/tmp/eirikr-ghidra /var/tmp/eirikr-ghidra/fscache2 /var/tmp/eirikr-ghidra/packed-db-cache /var/tmp/eirikr-ghidra/fscache2/.lastmaint /var/tmp/eirikr-ghidra/packed-db-cache/cache.map /var/tmp/eirikr-ghidra/packed-db-cache/pdb90255438 /var/tmp/eirikr-ghidra/packed-db-cache/pdb90255438/db.1.gbf
```

Decoder command: `PYTHON_INTERPRETER=$(command -v python3)` then `"$PYTHON_INTERPRETER" -B -` with the session-retained inline program. Algorithm: read five selected a.out/.dis pairs; unpack `<8I`; slice payload at byte 32; compare retained `.text` encodings with payload; decode non-directives with `Cs(CS_ARCH_ARM, CS_MODE_THUMB|CS_MODE_MCLASS)` and detail enabled; map immediate jump/call targets to containing non-dot symbol ranges; traverse supplied roots; enumerate register/interior targets; count candidate `.word` bytes. The probe did not import the supplied analyzer. Exact inline-source replay depends on the session transcript.

SHA-256 below was measured with `sha256sum` on each named input. Prefix S=`<audit scratch directory>/`, R=``.

```text
e9c8ed082c66d259987ceaaae5f0cc8cf4de10bd5c9044d93f6b269f3dfcaa90 S/aout-reachability-25-text-bounded.json
e233dd37dc3577c0830ba3e3b77b6f5cce089db0ceac6a3a729299533e437b58 S/aout-reachability-25-libgcc-filtered.json
2a01d22a613e351a050a07efbfa25368fb3082763ec05cb446dc7c6dfd0538d1 S/aout_reachability.py
2b3e403b8c7dc48d5690daad176bbb3a1cc1666af56d7ac3031802c13c87e7dc S/aout_archive_section_filter.py
8f272b2afdd2b48f2386826ce68675a246000fb7d40d913830592e6978ef2e71 R/usr.bin/awk/awk
d79552413847372593b4f26ecd3bc38c55ba26b18fd37378ea6b697d2f465fe1 R/usr.bin/awk/awk.dis
ad8b3ef374571b7fcfbb11d293d025844255c33956ba76630982f8bee9b3c4cd R/sbin/fsck/fsck
097ad41e7dab2d46f7d1d4d9602c5001b62c955f63c04a9d4e48f82fa4231f0b R/sbin/fsck/fsck.dis
a143f1e03ea00ecfc456f4f20be05dd81460385e383e0475be5c2ad68366e1e4 R/usr.bin/find/find
861718b60b3dc8cdc8ae196dcf568c168d320185fd58c2d2c65f9777ab20a377 R/usr.bin/find/find.dis
ce2842372f200bd81e75b0bbeb9a048801817e564f66f1499ee15aa980d43d5d R/libexec/getty/getty
b6088a0735ba7ae276fac37ae071c3fd4c36f11d40a4b383ed4b9170498548a8 R/libexec/getty/getty.dis
8724e1923002de86c44709d5d3c4570093a4ce1f3b7b136a06324e5fdc127465 R/usr.bin/re/re
1b00a977e9f3911ea0f2693ccabd332cae877fd11066caf3d70041d4130446c4 R/usr.bin/re/re.dis
8ca5acb823de98bdc9dabb875235e90a3b621b45d189c166bf0f17446564843c R/lib/libm/atan.c
a14a01fabe61c739bfa0259c96e4ff41a0eff50c9bcd2a5ba74458a98e551e40 R/lib/libm/asin.c
e2a987984734f5799ecf5daa3ff0a1e2ec139706d1ecbb1024b3e543b2dd20e8 R/usr.bin/re/r.window.c
31284c7f4c66adba0f064e54db71db820dd266377f48353a3282a067100dfbfd R/usr.bin/re/r.cmd.c
95cae3134b5a60697d18d016853ff7371be6daae58177448c2411f4a2a43896b R/usr.bin/re/r.edit.c
99b31af5b0bb32458b97b446c27e9cf04235f1befaef36fa8ae7ba499650d163 R/usr.bin/re/r.defs.h
bcf314926da1d79839130a7a73105a8149b3141696321dfc6f78c7aed130a235 R/usr.bin/find/find.c
2c7f50098ba0d3cf757189fd44de55e42688c80299ee3a27ee93014879f35559 R/sbin/fsck/main.c
745ef02fe4207d963ac9ccb8a001f9ebb78517be63f49210d0f7e545dd1b5863 R/lib/libc/stdio/findiop.c
b2335dea0d1a57408b9b67c4578d190ca5128073632656af086eaa3d1fda52b1 R/lib/libc/stdio/exit.c
3298066aef942fb18d853bc5f9e6cc2086e29febb2e73d5901aae95f530969a6 R/lib/libc/arm/sys/sigaction.S
5277feb9bcd171853523539cdd2651cb3be5d4e8864c4c86a6fad3c911649216 R/lib/libc/arm/sys/sbrk.c
c907de03928f46a0a2a8db5a0931b22bf79efb104459f393c5f80743723d9c2d R/lib/libc/stdio/scanf.c
```

The report establishes a bounded static disagreement and explicit unknowns. Relinked recovery, behavioral safety, and whole-corpus completeness remain outside its claims.
