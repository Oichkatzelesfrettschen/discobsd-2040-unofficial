# Dhara checkpoint pages torn by a processor reset

A processor reset inside the four chip programs of one Dhara checkpoint page
leaves a page whose magic and epoch are written and whose root metadata is
still erased. The vendored journal accepted such a page and the root mounted
blank (issue 619; the oracle and its cells are in
`flash-reset-recovery-cells.md`). `find_root()` in
`sys/arch/rp2040/dhara/journal.c` now accepts a checkpoint page only when
the sector tree it roots is whole, and otherwise falls back to the newest
whole checkpoint before it. The on-flash format is unchanged.

Evidence class: host model (the NOR model and extracted driver of
`check-flash-reset-cells`, strict C17 under ASan and UBSan). No board ran;
the reset hook that would reach these cuts on silicon is not built.

## Mechanism of the defect

The RP2040 boot ROM programs a 1 KiB Dhara page as four 256-byte chip page
programs, each self-timed and complete once /CS closes, and a page program
cut on a byte boundary keeps a byte prefix of its page. A reset therefore
leaves bytes `[0, k)` of the intended checkpoint page programmed and bytes
`[k, 1024)` erased, for any k. The page layout (header 16 bytes, cookie
4, seven 132-byte metadata slots, 80 bytes of erased padding) puts the
magic and epoch in bytes 0 to 3 and the root's metadata (slot 6) in bytes
812 to 943, inside the fourth chip page. Upstream `find_root()` checks only
the magic and epoch, so every k from 4 to 943 gave a page it accepted whose
root id or alt pointers read erased: the map lost the sectors behind them.

A second path appeared when the oracle cut a checkpoint in block 0 of a
wrapped journal (row `Kw-H2`): a page torn after its magic but before its
epoch made `find_checkblock(0)` hand `dhara_journal_resume()` the erased
epoch 0xff, no checkpoint of the lap matched it, and resume failed (blank
root). The shipped oracle rows never placed a tear in block 0 after a wrap,
so this path was not in the recorded 40 cuts per row.

## The rule

Three parts of one local change in `journal.c`, listed as one row of
`dhara/README.md`:

1. `hdr_is_checkpoint()`: a page is a checkpoint only when it carries the
   magic and its tail is below the journal's page count. Used by
   `find_checkblock()` and `find_root()`.
2. `cp_tree_whole()`: starting at the group's last user page, walk the
   radix tree through the alt pointers, reading each 32-bit field from the
   chip. Accept when no id is erased (0xffffffff, the value
   `map.h` reserves), every alt pointer is erased or a user page
   (`metadata_page_valid()`), and the nodes number exactly the sector count
   in the cookie, which must not exceed the page count. A root whose id is
   erased is the empty map and is accepted only with count 0. A map of one
   live sector whose id reads 0xff in the high byte is refused (see the
   one-sector case below).
3. `find_root()` tries candidates newest first with both tests, and when a
   block holds no acceptable checkpoint it steps to the previous block, up
   to `DHARA_MAX_RETRIES` blocks, decrementing the expected epoch when it
   steps from block 0 into the last block. `find_head()` still starts from
   the newest programmed group, so the head lands after the rejected page.

### Why it is sound

Accepted states. Every checkpoint the journal completes passes. The cookie
equals the tree's node count because `map.c` sets the cookie before every
operation that pushes metadata: `prepare_write()` (count after a new sector),
`raw_gc()` and `pad_queue()` (count unchanged, tree unchanged), and
`try_delete()` (count minus one, the deleted node removed from the tree);
`dhara_map_clear()` and the last-sector delete leave root NONE and count 0,
and the next `pad_queue()` writes cookie 0 with an erased root id. The tree
shape (each sector one node, reached through alt pointers of strictly
increasing depth) is the one `trace_path()` follows. The count never
exceeds `dhara_map_capacity()`, which `prepare_write()` enforces, and tails
and alt pointers are page indexes the journal itself assigns. Sector ids
come from `flstrategy()`, which adds the block number to the partition
offset of the MBR entry (`fl_setup()` copies the entries from sector 0
without clamping them to `flblocks`), and from `tools/flashimg`, which stops
at the capacity. So ids are not bounded by the page count, and the rule
does not assume they are: with two or more live sectors it accepts every id
from 0 to 0xfffffffe, and it refuses a one-sector map at an id of 0xff000000
or above, which needs a block offset of about 4 TiB (the one-sector case
below).

Rejected states. In a page torn at k, each 32-bit field (stored little
endian, `bytes.h`) is either intact, entirely erased, or has its high byte
erased; a high byte of 0xff exceeds the page count (below 2^24). So a torn
field turns an alt pointer into NONE (its subtree, at least one sector, is
no longer reached: count mismatch) or out of range (rejected), the root id
into NONE (empty tree against a nonzero count, which is intact because it
precedes the root slot) or a value whose high bytes are erased, and the count
or tail into out-of-range values (rejected). The root's slot is the last in
the page, so a cut inside its id also leaves every alt pointer erased; with
two or more live sectors the root has an alt pointer to the rest of the tree,
the walk reaches one node, and the count test rejects the page whatever the id
reads. That is why the rule needs no test of the id's value there, and
accepts ids with a high byte of 0xff (the `high` scenario of
`tests/rp2040/dhara_trigger`).

One-sector case. With one live sector the root has no alt pointer, so a cut
inside the id leaves a page that equals the complete page of the id with
those high bytes at 0xff: the page does not tell a torn id from a genuine
one, and no rule over the page can. The walk refuses a one-sector root whose
id reads 0xff in the high byte (row `K1-H2`), the form every such cut
produces, and the previous checkpoint answers; a genuine one-sector map at an
id of 0xff000000 or above is refused with it. Accepting it takes a commit
marker in the page, a format change (`dhara-audit.md`). A
page torn so that every erased field was already erased in the intended
page equals the intended page and is accepted, which the tear column
permits.

Fallback. Only the page in program at the reset can be torn, and the
checkpoint before it completed before the torn page's first command
began. Pages a previous fallback rejected stay on the chip until the head
erases their block and are rejected again by the same test, so consecutive
tears step back one block each. That case is argued, not exercised: after
each cut the oracle writes a whole checkpoint before the next reset, so a
rejected page never becomes a candidate again, and no run reaches the
eight-block limit. The restored tail is the predecessor's;
when the torn group's last user page was written, `prepare_head()` had
already refused to approach that tail's block, so placing the head past the
torn block cannot overrun it.

### Residuals

- A root the old kernel already damaged. If the old `find_root()` accepted
  a torn page whose count was intact but whose tree had lost sectors, the
  map kept that count, and every checkpoint written since carries a count
  above its node count. The old kernel mounts such a root (missing those
  sectors); this rule rejects all of its checkpoints and the root mounts
  blank. Inferred from `map.c`, not measured; the tear it needs falls in
  the last chip program of a checkpoint, on a reset or a power loss. The
  owner decides between reflashing such roots and a compatibility step:
  when no whole checkpoint lies within the eight blocks, accept the newest
  page that carries magic, tail and epoch, as upstream did.
- More than `DHARA_MAX_RETRIES` (8) consecutive torn checkpoints with no
  whole one between them: resume fails as it did before (blank root).
- `fl_setup()` discards the status of `dhara_map_resume()`, so a failed
  resume mounts an empty map silently, as before.
- Power loss can leave bits partly programmed, which the prefix model above
  does not cover; a partly programmed alt pointer could name an in-range page.
  The rule is claimed for processor reset only, as the oracle models.
- The walk costs at most (count + 1) node visits of up to 33 four-byte
  reads at every mount; boot time on the board is not measured.

## Alternatives

| Option | Format | Why not |
| --- | --- | --- |
| Reject a page whose root metadata tail is erased (page-local) | unchanged | unsound: a complete root legitimately has erased alt pointers, and the cuts 64 and 128 bytes into the fourth chip program leave the root id written with later alt pointers erased |
| Program the header chip page last | unchanged | a byte-prefix cut of that last program leaves the magic and epoch with an erased tail, counters and slots 0 and 1 |
| Program the page with byte 0 erased, then byte 0 alone | unchanged | the boot ROM programs 256-byte pages, so the second program clocks 0xff over programmed bytes; W25Q16JV Page Program (02h) is specified for previously erased (FFh) locations, and the NOR model counts it as a fault |
| A commit marker in the 80 erased padding bytes, or a checksum | changed (owner decision D12) | sound by itself, but old roots carry no marker and every root would be reflashed, or a migration rule would keep the defect for the first new checkpoint |
| Replay the root's metadata through `trace_path()` from the previous root | unchanged | `try_delete()` writes metadata `trace_path()` does not produce; more map logic in the journal |

## Cost

Predicted before measuring: kernel text +200 to +350 bytes in each of the
four kernels, data and bss unchanged, about 200 bytes of extra stack in
`find_root()` at mount (the walk's 33 page numbers and 33 levels).
`trace_path()` and `try_delete()` already hold 264 to 400 bytes of
metadata buffers on the stack during writes.

Measured (arm-none-eabi-gcc 14.2.1 20241119, ALIENWARE WSL):

| Kernel | text before | text after | delta | data | bss | RAM free |
| --- | --- | --- | --- | --- | --- | --- |
| PICO | 102451 | 102867 | +416 | 272 (+0) | 42928 (+0) | 23120 (+0) |
| PICO_UART | 91913 | 92329 | +416 | 216 (+0) | 18208 (+0) | 39648 (+0) |
| PICOCALC | 122795 | 123211 | +416 | 392 (+0) | 55904 (+0) | 10624 (+0) |
| PICOCALC_PSRAM | 125315 | 125731 | +416 | 448 (+0) | 56064 (+0) | 10048 (+0) |

`journal.o` alone: text 2406 to 2822 bytes (+416). PICOCALC_PSRAM keeps
4656 bytes of flash headroom (`check-kernel-headroom`). The text prediction
missed high by about 70 bytes: `find_root()` and `cp_tree_whole()` are
inlined into `dhara_journal_resume()` (0x35a bytes) and `hdr_is_checkpoint()`
is emitted as its own function. Stack (`-fstack-usage`, same flags):
`dhara_journal_resume()` 80 to 248 bytes, plus 40 for `meta_field()` below
it, about 300 bytes at mount. The write path already nests
`dhara_map_write()` (168), `raw_gc()` (176) and `trace_path()` (176), and
`try_delete()` alone takes 320, so mount stays under the existing peak.

## Evidence

- `check-flash-reset-cells` with the flipped table: every row passes; the
  18 tear rows recover the previous or the new checkpoint at every cut.
  Against the unchanged `journal.c` the same table fails 19 rows (40 cuts
  each, 41 for `Kw-H2`).
- Three mutants in `check.sh`: the tree test bypassed fails Kf-min, Kf-H2,
  Kg-H2, Gk-H2, F-H2 and Kw-H2; the tail test removed fails Kw-H2; the id
  bound weakened fails K1-H2.
- A one-off sweep of every byte prefix 0 to 255 of every chip command
  (rows Kf-H1, Kg-H2, Gk-H2, F-H2, Kf-min, Kw-H2: 2,049 to 8,195 cuts each):
  no failure with the change; 924 to 941 failing cuts per row without it. A
  one-sector variant (every write to one block) fails exactly the three cuts
  inside the root id (bytes 45 to 47 of the fourth program) when the id
  bound is weakened, and none with it; the gate's prefix list gained 46 to
  keep that field cut.
- `check-renode` boots the PICO_UART kernel over a flashimg-built root in
  Renode 1.17.0 to a login prompt and a root shell, so the kernel mounts
  through the new resume path on the Cortex-M0+ model.
- Gates over the same `journal.c` (ALIENWARE WSL, exit 0 each):
  `check-flash-checkpoint`, `check-dhara-metadata`,
  `check-dhara-amplification`, `check-dhara-gc-trigger`,
  `check-storage-recovery`, `check-storage-recovery-models`,
  `check-storage-correctness`, `check-kernel-headroom` and
  `check-c17-source-denominator`. `check-flash-sparse` resumes a root
  `tools/flashimg` built from an fsutil image (77 live sectors) through
  `dhara_map_resume()`, so the tree test passes on a flashimg-written
  checkpoint.
