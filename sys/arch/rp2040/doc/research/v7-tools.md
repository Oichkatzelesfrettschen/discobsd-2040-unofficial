# V6/V7 Unix disk images: catalog, license, and two ports

Follow-up to `downloads-survey.md` task 1 and 2: extract and triage the
V6/V7 disk images, then port the best candidates. Scope: read-only
extraction into scratch, a license verification, a catalog diff
against this tree, and two ports landed in `sbin/textbox`.

## License

`titor-special_torrent/UNIX-Source-Code/uv6swre.zip` and `uv7swre.zip`
each carry `README.txt` (a pointer to a license) and `AncientUnix.pdf`
(the license itself, a January 23, 2002 letter from Caldera
International, Inc.). The grant covers 32-bit 32V Unix and 16-bit Unix
Versions 1-7 on the PDP-11, explicitly excluding System III, System V,
and successors. The body is a BSD-style 4-clause license (attribution,
no-endorsement, "AS IS" disclaimer) plus an advertising-clause
acknowledgement, reproduced verbatim in `usr.bin/look/LICENSE` and
`usr.bin/deroff/LICENSE`. It is PORTABLE without qualification for a
2.11BSD-derived target, confirming the survey's verdict.

The `202112` collection the survey named does not exist on this host;
only `titor-special_torrent` (7.6 GB) does, and it holds the same four
disk images inside the two zips above (RK05 packs in `uv6swre.zip`,
one RL02 pack in `uv7swre.zip`) plus the `AncientUnix.pdf` and Lions
Commentary scan the survey described.

## Extraction

No V6/V7 filesystem reader existed on the host or in the tools catalog when
the images were extracted. Open SIMH was installed later and now supplies the
host-only behavioral path in `legacy/pdp11-v6/tests/pdp11_reference`; it does not expose files
from an offline filesystem image. A reader was written against the documented
V7 on-disk layout, then corrected twice
against decisive checks against the images themselves:

- Root directory is inode 2 on V7 (inode 1 is unused), inode 1 on V6.
- V7's 64-byte dinode has `di_mode`/`di_nlink`/`di_uid`/`di_gid` as
  plain little-endian shorts; `di_size` and the three time fields are
  32-bit values stored as two 16-bit words, high word first.
- V7's 13-slot `di_addr` array (10 direct, 1 single-indirect, 1
  double, 1 triple) uses 3-byte little-endian block numbers -- but an
  **indirect block's own entries** use a different, wider encoding:
  4-byte hilo32 words (the same high-word-first packing as `di_size`),
  128 entries per 512-byte indirect block, not the 170 a 3-byte
  packing would give. This was found by decisive falsification: a
  first pass read indirect-block entries as 3-byte le24 (matching the
  inode's own addr array) and produced block numbers up to
  11,141,120 on a 20,480-block image -- clearly wrong -- and corrupted
  every file whose size passed 5,120 bytes (10 direct blocks) into its
  single-indirect range, `usr/src/cmd/deroff.c` among them. Re-reading
  the same indirect block as 4-byte hilo32 words instead yields block
  numbers 6058, 6061, 6064, 6067, continuing the file's own direct
  blocks (6025, 6028, ..., 6052, already stepping by 3) exactly, and
  block 6061 holds legible continuation of the C source. Re-extracting
  the full V7 tree with the fix produced zero high-bit-set bytes across
  every `.c` file, including the largest (`ed.c`, 24,850 bytes, four
  indirect-range blocks).
- V6's 32-byte dinode addresses are 2-byte little-endian slots, 8 of
  them, confirmed the same way (2-byte reads give a legible
  continuation directory and a valid `0407` a.out magic; 3-byte reads
  land outside the image).

The reader (`v7fsread.py`) and the one-shot extraction driver
(`build_catalog.py`) live in scratch, not in this tree -- they are
tools for reading images, not code this port ships. Layout found:
V7's single pack holds a complete `/usr/src` (1086 files) and
`/usr/include`; V6's four RK05 packs are `/` (root), `/usr`
(mountpoint for `/usr/source`), the actual `/usr/source` kernel and
utility source (595 files: `s1`-`s7`, `cref`, `fort`, `yacc`), and
`/usr/doc` (337 files, mostly troff tutorials). 2018 files cataloged
total; `catalog.json` (scratch) records path, image, size, and a
first-pass description for each.

## Catalog diff against this tree

Cross-checked against `bin`, `sbin`, `usr.bin`, `games` (the tree's
name for what the task calls `usr.games`) and `lib/libc` in this
worktree. Of the survey's own candidate list:

- `factor`, `primes`, `hangman`, `quiz`, `wump`, `banner`, and `bcd` already
  build in `games/`; the RP2040 image omits them (`STORAGE.md`).
- `tsort` and `col` already build in `usr.bin/`.
- `units` is absent. V7's `usr/src/cmd/units.c` uses `double` conversion
  factors and reads a 601-entry table from `/usr/lib/units` at run time. A
  direct port fits poorly on a target where floating-point `printf` costs
  about 10 KB and remains disabled by default. A fixed-point rewrite remains
  separate work.
- `look` was absent and is now ported below.
- `deroff` was absent and is now ported below.
- `bj`, the V7 blackjack game, is absent from the tree and both images as C
  source. Only the V6 manual `usr/doc/man/man6/bj.6` survives on
  `unix3_v6_rk.dsk`. Porting `bj` requires a clean-room implementation from
  the manual rather than modification of source covered by the Caldera grant.

A pass over the rest of V7's `usr/src/cmd` (89 further utilities)
found nothing else that is simultaneously absent from the tree, free
of floating point, and self-contained: most names (`cat`, `wc`, `pr`,
`nm`, `ar`, `sort`, `find`, `diff`, `tee`, `uniq`, `join`, `cmp`, `od`,
`strip`) are already shipped or built in this tree; the rest
(`quot`, `dcheck`, `icheck`, `restor`, `dump`, `iostat`, `prof`,
`dmesg`) are filesystem- or hardware-specific to the PDP-11's own disk
and terminal stack and have no board analog; `tabs` targets specific
1970s hardware terminals (DASI300, TN300, HP2645) irrelevant to a USB
or UART console; `random`, `spline` use `float`/`double`; `ptx` and
`diff3` are correct-but-heavier document tools that did not clear the
bar against `look`/`deroff` for this pass.

## What was ported

Both from V7's `usr/src/cmd` (Seventh Edition Unix, 1978-79), rewritten
with ANSI C prototypes for Smaller C (which rejects the originals'
K&R parameter-list definitions) and nothing else changed: same option
letters, same algorithm, same default paths.

**`look`** (`usr.bin/look/look.c`, from `usr/src/cmd/look.c`, 162
lines / 2120 bytes on the V7 image, entirely within its first 10 direct
blocks so unaffected by the indirect-block bug above). Binary-searches
a sorted file (default `/usr/dict/words`) for lines with a given
prefix; `-d` folds to alphanumerics only, `-f` folds case, `-t c` ends
comparison at character `c`. Useful for anyone who keeps a sorted
wordlist, changelog, or index file on the board and wants to jump to a
prefix without `grep`ing the whole thing.

**`deroff`** (`usr.bin/deroff/deroff.c`, from `usr/src/cmd/deroff.c`
v1.02, 24 July 1978, 495 lines / 6906 bytes -- past the 5120-byte
direct-block boundary, the file that found the indirect-block bug).
Strips troff/eqn/tbl markup from a document, leaving running text (or,
with `-w`, one word per line); follows `.so`/`.nx` include commands.
Useful for reading this tree's own `-mdoc`/troff-flavored man page
sources as plain text on a serial console with no `nroff`/`groff` on
board, or for a word-frequency pass over any document.

Neither uses `long long` or floating point. Both link the tree's
present headers (`ctype.h`, `stdio.h`, `stdlib.h`, `string.h`)
unmodified.

## Build and size

Both are members of `sbin/textbox` (see its Makefile: each tool's
object is built in its own directory, relocatably linked with
`ld -r`, `main` renamed and localized, then linked once against
`libc.a`) rather than standalone a.outs, per the tree's own accounting
that a standalone a.out duplicates about 8 KB of libc per copy.
Standalone builds exist too (`usr.bin/look/Makefile`,
`usr.bin/deroff/Makefile` build them on their own for host testing and
size measurement) but are not what ships.

Cross build, `arm-none-eabi-gcc -Os -Wall`, zero warnings from either
file:

    bmake -C usr.bin/look MACHINE=rp2040
    # standalone: text+data+bss = 9,821 bytes
    bmake -C usr.bin/deroff MACHINE=rp2040
    # standalone: text+data+bss = 13,750 bytes

`sbin/textbox`, before (12 sbase tools) and after (14, with `look` and
`deroff` added to `TOOLS=` in `sbin/textbox/Makefile`):

    before: text 31436  data  740  bss 1044  dec 33220  (a.out: 32188 bytes)
    after:  text 37300  data  752  bss 2404  dec 40456  (a.out: 38084 bytes)

The two tools together cost 7236 bytes of box growth -- less than one
standalone a.out's libc duplication overhead, and textbox stays inside
the tree's own "images stay near 40 KB" box guidance
(`sys/arch/rp2040/doc/STORAGE.md`). A full `bmake -k MACHINE=rp2040
build` from clean, with the two tools added, completes with no new
warnings or errors beyond the pre-existing ones `BOOT-MAP.md` already
documents (uucp's install, two man page names).

## Host test: differential against the V7 reference

Both were compiled with the host `cc -Wall -Wextra -std=c99` (no
warnings after one dangling-else brace fix in `deroff`'s `skeqn()`,
purely a brace-clarity change with no behavior difference) and their
output compared byte-for-byte against the original V7 source compiled
with `cc -std=gnu89` (the V7 `look.c`'s `puts(entry,stdout)` two-argument
call needed the one-line ANSI-`puts` substitution `puts(entry)` to
compile at all on a host libc; this substitution was made only in the
temporary host-reference copy, not in the ported source, and is exactly
what the rewrite already does):

    # look: prefix search, six words including a case-folded miss
    diff <(./look ban words.txt)   <(./look_orig ban words.txt)
    diff <(./look Alpha words.txt -f) <(./look_orig Alpha words.txt -f)
    # (six cases total, all identical)

    # deroff: a sample man page with troff markup, .EQ/.EN, both modes
    diff <(./deroff sample.roff)    <(./deroff_orig sample.roff)
    diff <(./deroff -w sample.roff) <(./deroff_orig -w sample.roff)

All diffs empty.

## Board smoke test

Not run (do not touch the board per task scope). After flashing an
image built from this branch:

    look ban /usr/share/dict/words   # or any sorted file on the board
    look -f Alpha /tmp/somefile
    deroff /usr/share/man/man1/ls.1  # or any local troff/man source
    deroff -w /usr/share/man/man1/ls.1 | head

Both dispatch through `/usr/bin/textbox`, hard-linked in
`distrib/rp2040/mi.rp2040`, the same way `cut`/`paste`/`seq` etc. do.
