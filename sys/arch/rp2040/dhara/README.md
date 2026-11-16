<!--
Copyright (c) 2026 DiscoBSD

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
-->

# Vendored Dhara

These files are Daniel Beer's Dhara flash translation layer, ISC licensed
(`LICENSE`), vendored from upstream commit 1b166e41b74b. Each file keeps its
upstream copyright header. `compat/` is this tree's: it supplies the three
standard headers a `-nostdinc` kernel lacks.

The kernel (`sys/arch/rp2040/compile/*/Makefile`), `tools/flashimg` and the
host gates under `tests/rp2040` and `tests/storage_recovery` compile these
sources, so one set of sources defines the on-flash format.

## Local changes

The files are no longer byte-identical to upstream. `git log -- .` in this
directory lists each change, and each one is listed here:

| File | Change | Commit subject |
| --- | --- | --- |
| `map.c` | the unused `meta_clear()` removed | macOS as a development host: build, gates and suites; discobsd-flash; DEVELOPMENT.md |
| `error.c`, `journal.c`, `map.c` | signed and unsigned comparisons and an unused parameter repaired for `-Wall -Wextra -Werror` | build: enforce fatal warnings where CFLAGS cannot reset them (#109) |
| `journal.c` | `dhara_journal_read_meta()` rejects a malformed metadata page index before it reads the metadata buffer (`metadata_page_valid()`) | rp2040: bound Dhara flash page accesses |
| `journal.c` | `find_root()` accepts a checkpoint page only when it carries a tail below the page count (`hdr_is_checkpoint()`, also in `find_checkblock()`) and the sector tree it roots is whole (`cp_tree_whole()`: no erased id, alt pointers erased or user pages, node count equal to the cookie), and otherwise steps back to the previous checkpoint, across blocks and the epoch wrap; `docs/research/dhara-torn-checkpoint.md` gives the rule and its soundness argument | dhara: accept a checkpoint page only when its sector tree is whole |
| `map.c` | `auto_gc()` starts collecting at journal capacity less the safety margin less live sectors / `gc_ratio` less one checkpoint group (`gc_trigger()`), not at `dhara_map_capacity()`; `sys/arch/rp2040/doc/research/dhara-gc-trigger.md` gives the bound and the board measurement | dhara: start collection by the live sector count |

`check-dhara-metadata` covers the metadata bound, `check-dhara-gc-trigger`
covers the collector trigger and `check-flash-reset-cells` covers the
checkpoint acceptance. Re-fetching upstream means reapplying the rows
above.
