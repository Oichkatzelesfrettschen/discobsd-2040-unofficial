# NSTATIC resident-footprint audit

The source-level 20-to-8 change removes 240 initialized-data bytes and 12 BSS bytes per executable image that includes the standard findiop implementation: 252 bytes of static resident objects. The authorized 25-image subset contains 20 positive findiop witnesses, supporting 4,800 data bytes + 240 BSS bytes = 5,040 bytes (4.921875 KiB) summed across those images. Those sums describe source-sized candidate reductions; rebuilt executable sizes, filesystem blocks, and simultaneous SRAM recovery remain unmeasured.

| Tool | Executable | Version / use |
|---|---|---|
| jq | /usr/bin/jq | 1.8.2; supplied JSON and arithmetic |
| rg | /usr/bin/rg | 15.2.0; exact permitted sources and retained disassembly |
| nm | /usr/bin/arm-none-eabi-nm | Binutils 2.47.20260726; rejects raw cat a.out, exit 1 |
| objdump | /usr/bin/arm-none-eabi-objdump | Binutils 2.47.20260726; version only |
| Python | /usr/local/bin/python3 | 3.14.7; resolved once with `PYTHON_INTERPRETER=$(command -v python3)`; 32-byte header arithmetic and metadata |
| sha256sum | /usr/bin/sha256sum | Coreutils 9.11; exact source/JSON hashes |

## Source arithmetic and behavior

`include/stdio.h:9-16` declares offsets `_cnt=0`, `_ptr=4`, `_base=8`, `_bufsiz=12`, `_flag=16`, `_file=18` on the observed ARM ABI. Two ints and two pointers contribute 16 bytes; two shorts contribute four bytes. FILE size and array stride are 20 bytes, aligned to four bytes, with zero internal or tail padding. Retained cat instructions load the pointer fields at +4/+8, the integer field at +12, and halfword fields at +16/+18; `_findiop` passes 20 to calloc and advances by 20. `_f_morefiles` advances the pointer vector by 80 bytes for 20 entries and advances each FILE address by 20.
`lib/libc/stdio/findiop.c:15-26` owns every NSTATIC-sized static array: initialized `_iob[NSTATIC]` and zero-initialized `sbuf[NSTATIC]`. `_iob` changes 400 -> 160 bytes in data; `sbuf` changes 20 -> 8 bytes in BSS. `_smallbuf` remains one four-byte initialized pointer; `iobglue` and `endglue` each remain one four-byte BSS pointer. The latter variables are scalars, not NSTATIC-sized arrays. Array-size arithmetic therefore gives exactly 240 data + 12 BSS object bytes. Both reductions are multiples of four; four-byte inter-object alignment introduces zero replacement padding. Larger section alignment and linker layout remain outside the measured object arithmetic.
`findiop.c:35-47,64-85` scans static slots first and, after exhaustion, allocates `4*nfiles` glue bytes, `nfiles` small-buffer bytes, and subsequent 20-byte FILE objects, where `nfiles=getdtablesize()`. Allocation metadata and allocator granules are additional and unmeasured. Eight static slots leave five beyond stdin/stdout/stderr. A ninth active FILE reaches dynamic allocation earlier; an all-static workload retains the full static-object reduction. `_f_morefiles` handles glue allocation failure, while the source assigns `_smallbuf=calloc(...)` and returns success without testing that allocation. `_findiop` can return NULL on a subsequent FILE allocation failure. `f_prealloc` can allocate remaining FILE slots eagerly. Runtime concurrency, maximum descriptor index, allocation pressure, and callers' failure handling require a separate audit; the dynamic path establishes capacity logic rather than runtime safety.

## Measured representative code/symbol evidence

Raw headers report zero symbol bytes; the retained `.dis` files expose text function labels and literal pools rather than a data-symbol table. The table names objects by matching source operations to measured literal addresses. `sbuf` has source-proved extent but an unresolved address because `_smallbuf`'s initialized payload lies beyond the permitted 32-byte binary header. A relinked symbol-size measurement is outside this audit.

| Image / retained witness | _iob base; source extent | iobglue; endglue | _smallbuf | Evidence |
|---|---|---|---|---|
| cat, build-tree representative | 0x20002638; 400 B | 0x200027d4; 0x200027d8 | 0x20002634 | cat.dis:792-939; _fwalk endpoint 0x200027c8, exactly base+400 |
| ps, manifest image | 0x20003b00; 400 B | 0x20003cb0; 0x20003cb4 | 0x20003afc | ps.dis:1826-1856; three literal words and four-byte glue offset |
| smlrc, manifest image | 0x2000ce40; 400 B | 0x200135ec; 0x200135f0 | 0x2000ce3c | smlrc.dis:15875-15905; identical 20-byte FILE and 80-byte vector-loop strides |

## Manifest distinct-image inventory

`distrib/rp2040/mi.rp2040:137-178` lists 36 `file` paths in executable directories. The supplied 204-program inventory intersects 25 of those paths; the supplied reachability JSON lists exactly those 25. Manifest hard links name shared images and add zero distinct storage images. Independent concurrent invocations of an image would each pay their own process allocation; applet names alone establish neither process count nor SRAM residency.

| Scope / classification | Exact image names | Count |
|---|---|---|
| Positive standard-findiop witnesses, /bin and /sbin | ps, fsck, init | 3 |
| Positive witnesses, /usr/bin | awk, sed, grep, fgrep, find, sort, login, passwd, su, kilo, menu, tail, du, as, ld | 15 |
| Positive witnesses, /usr/libexec | getty, smlrc | 2 |
| Selected images with absent _f_morefiles/_findiop/f_prealloc/_fwalk labels | sh, ed, re, tee, update; each has an alternative _cleanup label | 5 |
| Manifest files absent from supplied binary inventory | box, sysbox, adminbox, textbox, utilbox, gamebox, stevie, cc, nohup, true, false | 11 |

The five negative code-label cases establish absent standard findiop functions in retained text. ed (305 data bytes), tee (92), and update (8) additionally cannot contain the standard 400-byte initialized array. sh and re have larger data segments, so their array absence remains an inference from code, not an independently measured data-symbol absence. The 20 positive witnesses are the admitted payer denominator; the remaining five receive zero credited savings. Missing manifest images receive zero credited savings and retain an unknown status, including whether a listed path is a script. The audit excludes all internal multicall applets as separate images.

## Build-tree count and exact ceilings by scope

The supplied inventory contains 204 valid a.out program paths, 207 disassemblies, and three excluded entries: two kernel ELF paths and absent usr.bin/make/make. A fresh first-32-byte probe validates OMAGIC 0x107, text/data/BSS agreement, and zero symbol bytes for all 204 admitted binaries (6,528 bytes read). The build inventory contains 200 data segments of at least 400 bytes. ed, sync, tee, and update fall below 400 bytes. Counting 204 build outputs as shipped executable images would conflate different scopes; the audit establishes 21 positive findiop witnesses in the build tree, including cat, and leaves the remaining build-payer classification unmeasured.

| Scope | Exact source-sized candidate ceiling / admitted amount | Interpretation |
|---|---|---|
| One positive payer | 240 data B + 12 BSS B = 252 B | Static object reduction; actual linked segment delta unmeasured |
| 20 admitted manifest payers | 4,800 data B + 240 BSS B = 5,040 B = 4.921875 KiB | Sum over distinct image definitions, not simultaneous SRAM |
| All 25 selected images, pre-classification bound | 6,000 data B + 300 BSS B = 6,300 B = 6.15234375 KiB | Loose upper bound; five images lack a positive payer witness |
| Entire authorized build inventory | At most 200 possible 400-byte arrays: 48,000 data B + 2,400 BSS B = 50,400 B | Source-object ceiling over build paths; not root-image or SRAM recovery |
| Manifest outside selected binaries | Eleven paths unresolved; zero admitted bytes | Binary absence from the supplied inputs bars measurement |
| File-backed payload of 20 admitted images | 4,800 candidate data B; BSS contribution exactly zero | OMAGIC zero-fill BSS consumes resident allocation, not file payload |
| Root filesystem allocation | Unmeasured | File block rounding, packing, and regenerated root image require separate evidence |
| Simultaneous residency | 252*n static bytes for n concurrently resident positive-payer processes, before heap effects | n is unmeasured; sum across installed names is an invalid substitution |

## Constraints, falsifiers, and rejected estimates

The parent supplies clean main HEAD `ccfa7882e75414e2b28baa84c21b74f90b55d1a9`; this lane hashes authorized source files and leaves Git-state verification to the parent. `lib/libc/stdio/local.h` is absent (rg exit 2); the audit substitutes zero additional source paths. The only artifact written by this lane is this report. Source, permitted disassemblies, JSON metadata, and first-32-byte header probes form the evidence boundary; target execution, rebuilding, relinking, kernel ELF inspection, hardware, services, and network remain excluded. Conservative whole-file charging for each retained-disassembly scan plus source/JSON reads stays below 16 MiB; the selected 25 disassemblies total 8,003,189 bytes, and the raw header probe reads 6,528 bytes.
Falsifiers: a retained binary's header drifting from inventory invalidates that member; a mismatched disassembly invalidates its code witness; a different FILE stride invalidates 20-byte arithmetic; a relink that changes section padding invalidates any exact segment-size prediction; a greater-than-eight FILE workload or f_prealloc activity invalidates the all-static heap assumption; an independently proven different simultaneous-process count changes the SRAM sum. Current binary/disassembly identity comes from the supplied artifact provenance rather than a fresh full binary hash, because binary reads are header-bounded.
`constrained-c.md:331-364,390` claims 240 bytes times roughly 64 shipped files, around 15 KB, and labels that saving measured. The arithmetic 64*240=15,360 bytes=15 KiB is correct for that hypothetical denominator. The current manifest and admitted binaries reject the 64-payer denominator; hard-linked applet names are shared executable images, and several admitted images lack standard findiop text. The prose also omits 12 BSS bytes per payer, conflates root-file storage with resident allocation, and establishes neither a relinked delta nor runtime safety. Reject 15 KB/15 KiB as a measured current-root saving. Reject approximately 6.5 KiB as an admitted measured total: even the loose 25-image static-object ceiling is 6.15234375 KiB, and the 20 positive witnesses yield 4.921875 KiB. The full root remains partly unmeasured.

## Exact commands and source hashes

Commands ran from the shared host: `jq 'keys'` and scoped `.programs` projections on the two supplied JSON paths; `/usr/bin/rg -n '^'` on exact permitted source files; `/usr/bin/rg -n -C 8 'NSTATIC|15 KB|15.K|static.*FILE' sys/arch/rp2040/doc/research/constrained-c.md`; `arm-none-eabi-nm -S bin/cat/cat`; `/usr/bin/rg -n -A 170 '<_f_morefiles>:|<_findiop>:|<_fwalk>:' bin/cat/cat.dis`; `/usr/bin/rg -n -A 40 '<_f_morefiles>:'` on authorized tee/smlrc disassemblies. Manifest command: `/usr/bin/rg '^file /(bin|sbin|usr/bin|usr/sbin|usr/libexec|usr/games)/' distrib/rp2040/mi.rp2040`.
Selected-image witness loop: `while IFS= read -r disassembly_path; do /usr/bin/rg --no-mmap -H -n -m 1 -A 31 '^[0-9a-f]+ <_f_morefiles>:' "$disassembly_path"; done < <(jq -r '.programs[].disassembly' <audit scratch directory>/aout-reachability-25-text-bounded.json)`. Five-case check: `/usr/bin/rg --no-mmap -H -n '^[0-9a-f]+ <(_f_morefiles|_findiop|f_prealloc|_fwalk|_cleanup)>:'` with exact sh, ed, re, tee, update `.dis` paths from that JSON. The header probe resolves Python once, reads the supplied inventory with json.load, then for every `.aout` opens `rb`, reads exactly 32 bytes, unpacks `<8I`, and asserts words 0/1/2/3 equal OMAGIC/text/data/BSS; word 5 is zero for all 204. The probe uses os.stat only for disassembly and JSON size accounting.
SHA-256 authorities: `findiop.c=745ef02fe4207d963ac9ccb8a001f9ebb78517be63f49210d0f7e545dd1b5863`; `include/stdio.h=7e858b1ae15360a6d43e988fdb36560ded5111e9cf15591061b605fbc3731020`; `mi.rp2040=2ffe8e0e0ab413a02b20018271ff9d93577b515c76ac75ea5a97293fc176b558`; `constrained-c.md=c7468fcb96eff82c687756abb6a78918500a22266b363b145d3279793e0ea2ca`; `aout-program-inventory.json=160e22610830fb369fc5016ba13b176ec58e8dada84eab8d10480446e4f4d152`; `aout-reachability-25-text-bounded.json=e9c8ed082c66d259987ceaaae5f0cc8cf4de10bd5c9044d93f6b269f3dfcaa90`. Replay hash command: `sha256sum` followed by these exact six authorized paths.
