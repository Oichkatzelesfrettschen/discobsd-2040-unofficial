<!--
Copyright (c) 2026 GizmoBSD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
| `map.c` | the unused `meta_clear()` removed | macOS as a development host: build, gates and suites; gizmobsd-flash; DEVELOPMENT.md |
| `error.c`, `journal.c`, `map.c` | signed and unsigned comparisons and an unused parameter repaired for `-Wall -Wextra -Werror` | build: enforce fatal warnings where CFLAGS cannot reset them (#109) |
| `journal.c` | `dhara_journal_read_meta()` rejects a malformed metadata page index before it reads the metadata buffer (`metadata_page_valid()`) | rp2040: bound Dhara flash page accesses |
| `journal.c` | `find_root()` accepts a checkpoint page only when it carries a tail below the page count (`hdr_is_checkpoint()`, also in `find_checkblock()`) and the sector tree it roots is whole (`cp_tree_whole()`: no erased id, alt pointers erased or user pages, node count equal to the cookie), and otherwise steps back to the previous checkpoint, across blocks and the epoch wrap; `docs/research/dhara-torn-checkpoint.md` gives the rule and its soundness argument | dhara: accept a checkpoint page only when its sector tree is whole |
| `map.c` | `auto_gc()` starts collecting at journal capacity less the safety margin less live sectors / `gc_ratio` less one checkpoint group (`gc_trigger()`), not at `dhara_map_capacity()`; `sys/arch/rp2040/doc/research/dhara-gc-trigger.md` gives the bound and the board measurement | dhara: start collection by the live sector count |

`check-dhara-metadata` covers the metadata bound, `check-dhara-gc-trigger`
covers the collector trigger and `check-flash-reset-cells` covers the
checkpoint acceptance. Re-fetching upstream means reapplying the rows
above.
