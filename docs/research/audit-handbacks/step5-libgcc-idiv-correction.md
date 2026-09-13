# AEABI signed-division provenance correction

The five `__aeabi_idiv` rows in menu, tail, du, as, and ld are **rejected as independently discardable libgcc bytes**. Their 198-byte analyzer total contains **190 bytes of signed helper code plus 8 bytes of inter-section padding**. The code belongs to the retained libc helper implementation, rather than the `_divsi3.o` implementation named by the filter.

## Exact reconciliation

| Program | Signed entry | Analyzer span | Code / padding | Unsigned entry | Reachable incoming unsigned callers |
|---|---|---:|---:|---|---|
| menu | 0x20001620 | 40 | 38 / 2 | 0x200015f4 | qst, _doprnt |
| tail | 0x20000cb4 | 40 | 38 / 2 | 0x20000c88 | strerror |
| du | 0x20000da4 | 40 | 38 / 2 | 0x20000d78 | strerror, _doprnt |
| as | 0x20004fc0 | 40 | 38 / 2 | 0x20004f94 | getexpr, makecmd, _gettemp, _doprnt |
| ld | 0x20003a84 | 38 | 38 / 0 | 0x20003a58 | lookup, readhdr, fread, _gettemp, strerror, strtol, _doprnt |
| Total | | 198 | 190 / 8 | | |

The independent Capstone pass examined the five retained `.text` disassemblies against their a.out payloads. Numeric direct call/jump targets, including conditional branches and interior targets, produced **zero incoming edges from another function into each signed span**. An unaligned four-byte scan of every selected text/data payload produced **zero stored words addressing any signed entry or interior**. Each unsigned entry is reachable from the supplied entry/stored-pointer root set through numeric direct edges. The signed helpers also call their unsigned counterparts. Register-call targets remain outside complete resolution; the positive section-retention result does not depend on proving signed runtime reachability.

The retained `lib/libc/arm/gen/aeabi_div.o` is 820 bytes. Its allocated executable `.text` is **82 bytes**, aligned to two bytes. The unsigned aliases `__aeabi_uidiv` and `__aeabi_uidivmod` start at section offset 0; the signed aliases `__aeabi_idiv` and `__aeabi_idivmod` start at **0x2c**. The signed instruction body is consequently **0x52 - 0x2c = 38 bytes**, not 40. Source `aeabi_div.S:14,21-22,51-52` places both pairs in one `.text`; `:63` calls `__aeabi_uidivmod`.

For each final binary, the complete 82-byte unsigned-plus-signed sequence matches the object `.text` except byte offset **0x42**, inside the recorded `R_ARM_THM_CALL` relocation at **0x40**. The relocation names `__aeabi_uidivmod`. The final encoding `ff f7 de ff` resolves to the unsigned alias 44 bytes before the signed entry. Every final 38-byte signed body is identical. Four final spans add `00 00` after the return and before `_brk`; ld starts `bcopy` immediately after the return. Those eight zeros lie beyond the helper object's 82-byte section.

The unsigned functions' reachability retains the single libc `.text` section under section garbage collection, including all 190 signed code bytes. Independently dropping those bytes requires changing the source/object section structure or the linker transformation; the requested audit performs neither action. The eight layout-padding bytes receive a separate classification because the inspected object does not own them. A new layout must measure their fate before assigning recovery.

## Filter error and aggregate consequences

`aout_archive_section_filter.py:145-156` collects every name match in `fallback_nodes`, then returns `exact_size_nodes or fallback_nodes`. A size mismatch therefore selects the same-name archive implementation. The parent supplied the current libgcc evidence: `_divsi3.o` has a 168-byte `.text`, `__divsi3` has size 146, `__aeabi_idiv` is a zero-sized alias, and `__aeabi_idivmod` starts at +0x94. That shape differs from the independently inspected libc object: an 82-byte section and signed aliases together at +0x2c. The 38/40-byte final spans cannot establish provenance in the libgcc object through name equality.

The matcher also assigns nodes to every final function before constructing its retained-node set (`:171-180`). A collision can therefore contaminate both candidate ownership and retained roots in the archive graph. Exact name-and-size equality would still be insufficient owner proof when implementations collide; actual selected input members, relocations, aliases, and normalized bytes are the discriminating evidence.

The supplied JSON records the arithmetic identity:

`21210 raw = 8248 co-retained + 198 discardable + 12764 unmapped`

`12962 post-filter = 21210 - 8248 = 198 + 12764`

The audit applies these claim boundaries:

- **Withdraw** the 198-byte independently discardable libgcc claim. Replace its five rows with 190 bytes positively co-retained with the reachable libc unsigned helper, plus 8 bytes of external alignment/padding whose independent recovery is unproved.
- **Withdraw** any presentation of 8248 as independently verified current-libgcc ownership or exclusion. Preserve 8248 as the original filter's attribution ledger, conditional on collision-free selected-member provenance. The JSON has one co-retained `_divsi3.o:.text` occurrence, passwd `__divsi3` at 148 bytes. The difference between its span and the parent's 146-byte symbol can reflect padding; this bounded follow-up neither rejects nor admits that separate row. Other co-retained groups were outside the five-helper validation.
- **Withdraw** 12962 as a validated recoverable or post-provenance ceiling. Preserve 12962 solely as the prior algorithm's candidate arithmetic. If all original 8248 exclusions are subsequently validated, excluding the independently proved 190 libc bytes yields a conditional **12772-byte** ceiling, which still contains 8 unproved padding bytes. Excluding those 8 on a measured layout basis would yield **12764**. Subtracting all 198 immediately would hide the code/padding distinction.
- The filter's 12764 unmapped bytes remain its original lexical classification. Reclassifying the disputed rows as merely unmapped would restore `12962 = 12764 + 198` without proving recovery; the retained libc section provides the stronger 190-byte exclusion.
- Where ownership for the 8248-byte subtraction remains unresolved, **21210** is the retained raw candidate denominator. A model that applies only this new 190-byte exclusion yields **21020**, rather than a validated recovery total. The denominator still inherits the analyzer's code-span, root, and decoder limits described in the earlier handback.

The falsifier for the libc-section conclusion is a selected-link provenance record showing a different input owner or a linker transformation that separates the signed bytes while preserving the observed 82-byte sequence. The falsifier for the direct-call absence is a decoded entry/interior transfer or a resolved register target into a signed span. Future revisions should match selected input ownership first, preserve alias/interior edges, and retain unknown attribution when ownership cannot be proved.

## Commands and hashes

Observed HEAD: `386b8f7239e02b5f6901583cbc69d648c91138d2`. The tools are the versions recorded in `step5-omagic-crosscheck-handback.md`: arm-none-eabi-binutils 2.47-1.1, jq 1.8.2-1.1, Python 3.14.7, Capstone module 5.0.7, and LIEF module 0.17.0-. The following commands are read-only:

```sh
REPO=<repository root>
SCRATCH=<audit scratch directory>
jq '.programs[]|select(.program|IN("menu","tail","du","as","ld"))|{program,discardable_archive_bytes,discardable_archive_candidates}' "$SCRATCH/aout-reachability-25-libgcc-filtered.json"
/usr/bin/rg -n '^' "$REPO/lib/libc/arm/gen/aeabi_div.S"
/usr/bin/rg -n -A 15 '^def match_archive_nodes' "$SCRATCH/aout_archive_section_filter.py"
arm-none-eabi-nm -S -a "$REPO/lib/libc/arm/gen/aeabi_div.o"
arm-none-eabi-objdump -h -dr "$REPO/lib/libc/arm/gen/aeabi_div.o"
jq '[.programs[]|.program as $program|.co_retained_archive_candidates[]|select(any(.archive_nodes[];startswith("_divsi3.o:")))|{program:$program,names,size,archive_nodes}]' "$SCRATCH/aout-reachability-25-libgcc-filtered.json"
git -C "$REPO" -c core.fsmonitor=false rev-parse HEAD
sha256sum "$REPO/lib/libc/arm/gen/aeabi_div.o" "$REPO/lib/libc/arm/gen/aeabi_div.S"
```

The exact object-byte comparison ran as:

```sh
PYTHON_INTERPRETER=$(command -v python3)
"$PYTHON_INTERPRETER" -B - <<'PY'
import lief,pathlib,json,hashlib
base=pathlib.Path('<repository root>');scratch=pathlib.Path('<audit scratch directory>');object_path=base/'lib/libc/arm/gen/aeabi_div.o';obj=lief.parse(str(object_path));code=bytes(obj.get_section('.text').content)
for program in json.loads((scratch/'aout-reachability-25-text-bounded.json').read_text())['programs']:
 if program['program'] not in {'menu','tail','du','as','ld'}:continue
 candidate=next(row for row in program['candidates'] if '__aeabi_idiv' in row['names']);binary=pathlib.Path(program['aout']).read_bytes();offset=32+candidate['address']-0x20000000-44;final=binary[offset:offset+82];assert len(final)==len(code)==82
 differences=[index for index,(left,right) in enumerate(zip(code,final)) if left!=right];assert all(0x40<=index<0x44 for index in differences)
 print(program['program'],'82-byte-object-match-except-relocation',differences,'final-relocation',final[64:68].hex())
print('object_text_sha256',hashlib.sha256(code).hexdigest())
PY
```

A separate inline Capstone probe in the session transcript parsed numeric transfers from all retained `.text` encodings, checked each encoding against the a.out payload, traversed the supplied roots, and scanned every four-byte payload window against the signed interval. The input/output signature is the five-row table above. The earlier handback documents the same decoder procedure and its limits.

SHA-256 values, with paths relative to the canonical repository except the two explicitly labeled derived byte sequences:

```text
c2b58eddf8d14884ce4755c610b827e537da9849a2633190ee58a9070f96c588 lib/libc/arm/gen/aeabi_div.o
534eda97c880cab347c794870c39ab828478a7806e936ab03f71226fcac51606 lib/libc/arm/gen/aeabi_div.S
3aff7d643dacd452e271f894322c028d4a00880b678967fbd2c1beec9a49da8c derived object .text, 82 bytes before relocation
7a84c7f10d7034b095cc00fa1fda6314e8a84ccb061fe2d52422835d86500018 derived final signed code, 38 bytes, all five binaries
5eb0a6e8e5e3f566033e865a83194b6df9609632952a6c24990aa5acc7d3be71 usr.bin/menu/menu
bbe79f79903814137bf074ed7660dfea419a22338532c46627c4bdb2a53b872d usr.bin/menu/menu.dis
d4bc3b5ebca10037d9760e0f162ee38322716aeb2d044cb672f27c13c0afcff9 usr.bin/tail/tail
9de4520f3c57ad051f6a7fdabb54b91c1d96114ef13f218f1d5ddc47dda29c9d usr.bin/tail/tail.dis
69558d7e9f2da47d2050f44bccf33e0f24674b84d4eea91f6da4511097290a61 usr.bin/du/du
28735d3211c6d801744f60d9a3497c9187b753921ace37fe9afaf7de14f908af usr.bin/du/du.dis
c48973e0b97980d444f47acc040c17e4adec0a1e3d9a728144195042abdd5fcb usr.bin/as/as
e21471b834be8f0c67e4e46ada796c6a124325f1e662657295e149267648bbf9 usr.bin/as/as.dis
de4832942b98e4f6220d1672eacb664015bf83c59a815a7557b1b48c6846c37f usr.bin/ld/ld
8411ff5dc7936bc0f1273ac3da9845e8a291b69bc565659044d709172ab0226d usr.bin/ld/ld.dis
```

The JSON/analyzer hashes remain those recorded in the preceding handback. The libgcc object shape is parent-supplied evidence; this addendum independently inspects the retained libc object and final binaries, rather than reopening libgcc.

## Write boundary

This follow-up's sole requested write is this addendum under `<audit scratch directory>`. The issued follow-up probes read files and print stdout; they request zero repository/build/device/service/network mutations. This subagent never targeted `/tmp/rp2040_build.log`; that artifact does not belong to its work. No Ghidra invocation occurred in the follow-up. The preceding handback records the earlier observed ancillary files under `/var/tmp/eirikr-ghidra`: `fscache2/.lastmaint`, `packed-db-cache/cache.map`, and `packed-db-cache/pdb90255438/db.1.gbf`. Those files were preserved. Filesystem-wide before/after write measurements were unavailable, so the claim is limited to issued operations and observed artifacts.
