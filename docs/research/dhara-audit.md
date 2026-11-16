# Dhara journal audit: torn checkpoints, damaged roots and a failed resume

This note audits the flash root's Dhara journal against alternative designs
for this device. It decides two questions that PR 636 (the torn-checkpoint
fix) leaves to the owner: how mount treats a root that the old kernel already
damaged by accepting a torn checkpoint (D26), and what `fl_setup()` does when
`dhara_map_resume()` fails (D28).

Evidence classes. Tags: **S** source read at the named revision, **M**
measured here (the appendix holds the sources and commands), **C** cited from
a named document or published source, **I** inference. No board, no Renode
and no kernel link ran for this note. The experiments are host runs of the
vendored Dhara over a NOR model of the root region. Tears are cut either as
a byte prefix of the in-flight 1 KiB page (the processor-reset model of
`flash-reset-recovery-cells.md`) or as a random subset of the zero bits of
its last 256-byte chip program (a power-loss model assumed here, not taken
from the datasheet).

Revisions and paths:

- `origin/main` f4066c03; a bare `journal.c`, `map.c` or `journal.h` is under `sys/arch/rp2040/dhara/`; `flash.c` and `flash.h` are under `sys/arch/rp2040/dev/`; `STORAGE.md` is under `sys/arch/rp2040/doc/`; `init_main.c` is `sys/kern/init_main.c`.
- PR 636 branch `rp2040/dhara-torn-checkpoint` c439e459. "636:" plus a line is its `journal.c`; `dhara-torn-checkpoint.md` is on that branch.
- PR 620 branch `tests/flash-reset-cells` 6d59d4cc. `flash-reset-recovery-cells.md` and the oracle `check-flash-reset-cells` are on that branch (PR 636 carries both).
- Compilers. The host harness uses GCC 16.2.1. Object sizes come from arm-none-eabi-gcc 16.2.0 with the PICO kernel's compile flags (`-Os -mcpu=cortex-m0plus -mthumb`, taken from `bmake -n` in `sys/arch/rp2040/compile/PICO`). PR 636's own figures were taken with arm-none-eabi-gcc 14.2.1, and no delta below crosses the two compilers.

## Finding

1. **The tear comes from the format, not from the fix.** One checkpoint page is four 256-byte chip programs, and recovery trusts its first four bytes. Under the prefix model, the old kernel keeps sector 0 readable only when the cut falls after the alt pointer that leads from the root to sector 0. For a root sector id of 1 or more, that band is the last 4b bytes of the root slot, where b is the bit position of the highest set bit of the id: 12 bytes (cuts 932 to 943) for id 13, 4 bytes (940 to 943) for id 3 and 36 bytes (908 to 943) for id 601 (M). Below id 1,024 that is at most 36 bytes, about 14 microseconds of clocking (I, from the layout and those three runs). Root id 0 is the exception (M): the root already is sector 0, so no alt pointer leads there, and every cut from the end of the id field to the end of the slot (816 to 943) keeps sector 0 readable while 1 to 699 other sectors read blank or unreadable. That band is 128 bytes, about 49 microseconds of clocking (I), 3.6 times the id 1 and up maximum. How often a root carries id 0 is not measured: the last page of a checkpoint group must hold sector 0, as a write or a garbage-collection copy (I). Outside the band, cuts from byte 4 to the band's start leave sector 0 blank or unreadable (M), and `fl_setup()` then finds no partition table or returns ENXIO (`flash.c:898-909`). Cuts in the first four bytes reject the torn page and keep the previous checkpoint, and cuts after the root slot leave complete metadata (M, the cut table below); neither loses data. Such a root does not boot, and the user reflashes.
2. **D26 is real; how many roots in service carry it is unknown (I).** A root that the old kernel kept using after such a tear carries a sector count above its node count in every later checkpoint, and the gap never closes (M). PR 636 treats it in one of three ways (M), depending on what followed the tear:
   - With eight or more blocks written since, it rejects every checkpoint in its window and resumes blank.
   - With fewer, it steps back to the checkpoint before the tear. The lost sectors return, but every write the old kernel acknowledged since is dropped without a message.
   - When the old kernel could not checkpoint again (a torn pointer on the sync path), it recovers the pre-tear state in full.

   A `flash.uf2` reflash rewrites the whole region (M, picotool 2.3.1), and no USB-requested reset can land in the window (C). The damage therefore needs a power cut on a root that was never reflashed afterwards.
3. **The upstream fallback is rejected.** PR 636 offers it as owner option 1. It mounts such a root, and it brings issue 619 back for that root: after the fallback, 936 of 1,025 cuts of every later checkpoint leave a state outside the permitted set (M).
4. **A failed resume today is silent and destructive.** It mounts an empty map, and the kernel panics with "No root filesystem found!" (`init_main.c:126-128`), with no record of why. Any write into the empty map starts the journal again at block 0 and erases old blocks. The next resume still fails, so the new writes are lost as well (M).
5. **A host metadata scan salvages damaged roots.** Run over a `picotool save` dump, it recovers every damaged root measured here, including sectors the old kernel itself could not read. That holds only until collection laps the unreachable pages: past a wrap, the scan reproduces the old kernel's view and nothing more (M). It needs no kernel text.
6. **PR 636 rejected every power-loss tear in the bit model.** In that model it recovered the previous checkpoint for all 3,000 tears tried, with 1/2, 7/8 and 31/32 of the zero bits programmed. The old journal failed all 1,200 it was given (M). A CRC adds no measured protection in this model.

| Horizon | Design | Text (gcc 16.2.0, object) | RAM | Format | Falsifier | Deciding measurement |
| --- | --- | --- | --- | --- | --- | --- |
| Now | PR 636 tree walk. `fl_setup()` refuses a region that holds a checkpoint but no acceptable root, with a message, while a region whose blocks 0-7 read as erased still mounts empty (the appendix sketch lacks that erased test). Mount reports how many newer checkpoints it rejected. A host salvage scan is the recovery route | journal.o +396 B (PR 636); flash.o +92 B (refusal sketch, lower bound); the report is not built (I: tens of bytes) | static 0; resume stack 80 to 248 B (M) | unchanged | a D26 or torn image that the scan cannot rebuild before collection laps it; a fresh or `tools/flashimg` region that the refusal blocks | D26 rows in `check-flash-reset-cells` (an image damaged under the old `journal.c`, resumed under the new one); a fresh-region refusal case |
| Later, gated | Additive CRC-32 of each checkpoint page in its 80 erased padding bytes, checked first; the tree walk kept for pages whose CRC field is erased | journal.o +106 B over PR 636 (`crc32_update` is already linked) | 0 | additive: an old-journal kernel resumes a CRC-written root unchanged (M) | a board or richer power-loss model in which PR 636 accepts a page outside the permitted set; without one, the CRC buys nothing measured | H09's power-loss oracle and a board power-cut campaign |

## 1. Mechanism and tear windows

### Geometry and page layout (S)

- The region holds 192 blocks of 8 KiB, eight 1 KiB Dhara pages each (`flash.h:49-56`, `flash.h:82-84`).
- `choose_ppc()` (`journal.c:142-160`) gives a checkpoint period of eight pages: seven 132-byte metadata slots fit in 1,004 bytes and fifteen do not. So each block is exactly one group, seven user pages and one checkpoint page.
- The checkpoint page layout:

| Bytes | Field | Source |
| --- | --- | --- |
| 0-2 | magic "Dha" | `journal.c:26-38` |
| 3 | epoch (laps of the ring) | `journal.c:41-49` |
| 4-7 | tail | `journal.c:51-59` |
| 8-15 | bad-block counters | `journal.c:61-79` |
| 16-19 | cookie: live sector count | `journal.h:30`, `map.c:32-40` |
| 20 + 132 i | slot i: sector id, then 32 alt pointers, each 4 bytes little endian | `journal.c:89-93`, `map.c:42-60` |
| 812-943 | slot 6, the root of the radix tree; its alt pointer at depth d sits at 816 + 4d | slot arithmetic |
| 944-1023 | 80 erased bytes | `journal.c:82-86` clears them |

### Write path (S)

- `push_meta()` fills the header (`journal.c:837-841`) and programs the page with one `dhara_nand_prog()` (`journal.c:843`).
- `dhara_nand_prog()` makes one `flash_program()` call for 1,024 bytes (`flash.c:657-674`, `flash.c:581-604`). The boot ROM issues it as four page programs in address order (I; `flash-reset-recovery-cells.md`, "What a processor reset does to the chip").
- `map.c` stores the count in the cookie before every push (`map.c:241`, `map.c:253`, `map.c:368`, `map.c:484`).
- A checkpoint is written whenever a group fills, and `dhara_map_sync()` pads the open group so that it fills (`map.c:510-530`). `DIOCCHECKPOINT` from sync, fsync, update(8) and the last close reaches that padding (`flash.c:769-803`, `STORAGE.md:276-296`).
- The kernel never trims or clears the map: no caller of `dhara_map_trim()` or `dhara_map_clear()` exists outside `dhara/` in `sys` or `tools` (M, `grep`).

### Recovery (S)

1. `dhara_journal_resume()` (`journal.c:426-469`) calls `find_checkblock(0)`. It looks at no more than eight blocks from block 0 (`journal.c:231-232`), and it tests only the magic (`journal.c:241`).
2. The journal's epoch comes from that page (`journal.c:438`).
3. A binary search for the last block of that epoch follows (`journal.c:253-283`), then a search for the last programmed group (`journal.c:314-343`).
4. `find_root()` accepts the newest page whose magic and epoch match (`journal.c:355-361`).
5. The tail and counters are restored from that page (`journal.c:453-455`), and the map takes its count from the cookie (`map.c:83`).
6. Resume issues no erase or program.
7. `fl_setup()` discards the result (`flash.c:877`).
8. A root that does not mount panics at `init_main.c:126-128`.

### Tear windows (M)

The scenario:

1. Write 700 sectors and sync.
2. Rewrite ten sectors.
3. Sync, and keep the first k bytes of that sync's checkpoint page.
4. Resume.

"prev" is the previous checkpoint's state and "new" the page's intended state. Rows are for rewrites of sectors 0 to 9, which give a root id of 13. Rewrites of 600 to 609, which give a root id of 3, move the sector-0 boundary from 932 to 940 and change nothing else; rewrites of 0 to 599, which give id 601, move it to 908. Rewrites of 1 to 6 give root id 0, and the table below that one gives the result (M; appendix sweeps). A count of blank or unreadable sectors covers sectors 0 to 699 that read blank, as an error or as garbage.

| Cut k | Upstream `find_root()` | PR 636 |
| --- | --- | --- |
| 0-3 | prev (no magic) | prev |
| 4-815, and every cut between chip commands | blank: the root id reads erased, so every sector is unmapped | prev |
| 816-931 | 15 to 699 sectors blank or unreadable; sector 0 blank or unreadable | prev |
| 932-943 | 1 to 7 sectors blank or unreadable; sector 0 readable | prev |
| 944-1024 | new | new |

The band by root sector id (M, old journal; the PR 636 journal returns prev for every cut from 0 to 943 and new for 944 to 1024 in all four rows):

| Root id | Rewrite | Sector 0 readable, damaged tree | Band (bytes) | Other sectors blank or unreadable |
| --- | --- | --- | --- | --- |
| 0 | 1-6 | 816-943 | 128 | 1 to 699 |
| 3 | 600-609 | 940-943 | 4 | 1 |
| 13 | 0-9 | 932-943 | 12 | 1 to 7 |
| 601 | 0-599 | 908-943 | 36 | 1 to 187 |

For id 1 and up the band is 4b bytes (b is the highest set bit; I, a fit to the three ids above and the layout: the alt pointer for bit b ends at byte 944 - 4b). Id 1 gives an empty band (I, not run). Id 0 has no differing bit, so the band is the whole alt array.

- **Cut inside a pointer.** It leaves the pointer's high byte erased, so the pointer names no page. `metadata_page_valid()` (`journal.c:544-568`) reports `DHARA_E_CORRUPT_MAP`, and the old kernel fails reads and writes of the sectors behind that pointer with EIO.
- **Cut on a field boundary.** It erases the pointer outright, and those sectors read blank instead.
- **Timing (I).** Clocking takes about 100 microseconds for 260 bytes (0.38 microseconds per byte), and the whole four-program page about 1.5 ms (C, `flash-reset-recovery-cells.md` window table). A band of 36 bytes clocks in about 14 microseconds, and the 128-byte band of root id 0 in about 49.
- **Agreement.** PR 636's own sweep (C, `dhara-torn-checkpoint.md`) and its oracle agree. The oracle passes on the PR 636 tree here (M; appendix).

## 2. Alternatives

The mechanisms are described in this note's own words. Costs are for this kernel:

- The text budget is 4,656 B of flash headroom on PICOCALC_PSRAM after PR 636 (C, PR 636 measurement with arm-none-eabi-gcc 14.2.1).
- Free RAM is 10 to 23 KB depending on the kernel (C, brief).
- "Partial bits" means the power-loss model of this note.

| Design | Mechanism | On-flash format | Text and RAM here | Reset tear | Partial-bit tear | Wear | Migration |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Upstream Dhara (`main`) | newest page with magic and epoch is the root | as above | journal.o 2,418 B (M) | unsafe: cuts 4-943 (M) | unsafe: 1,200 of 1,200 seeds, 1,000 at 1/2 and 200 at 31/32 (M) | dynamic, journal ring | none |
| PR 636 tree walk | accept a page only if its tree is whole and its node count equals the cookie; step back up to 8 blocks (636:108, 636:401-464, 636:477-512) | unchanged | +396 B (M; +416 with 14.2.1 per PR 636); resume stack 80 to 248 B (M) | sound (M) | recovered the previous checkpoint for all 3,000 seeds (M) | unchanged | D26 roots resume blank or roll back (M) |
| PR 636 + upstream fallback | when no whole checkpoint lies in the window, accept the newest page with magic, tail and epoch | unchanged | +92 B over PR 636 (M; PR 636 estimated about 30) | sound on healthy roots (M); on a damaged root, 936 of 1,025 cuts of every later checkpoint are accepted wrongly (M) | as upstream on damaged roots (I) | unchanged | mounts what the old kernel mounted, the defect included |
| PR 636 + minimum-gap heal | in that case, accept the newest structurally sound page with the smallest count-minus-nodes gap, and rewrite the cookie to the node count | unchanged | +88 B over PR 636 (M); stack 264 B (M) | sound on healthy roots and after the first whole healed checkpoint (M); a tear in that first checkpoint is taken for the root when it loses no more sectors than the old gap (M: cuts 932, 936, 940 lose 7, 3 and 1 sectors) | not run | unchanged | heals the erased-pointer class; the torn-pointer class still resumes blank (M) |
| Additive CRC-32 in the padding | CRC over bytes 0-943 stored at 944; a valid CRC accepts, an erased CRC field falls back to the tree walk, any other value rejects | additive; older kernels ignore the padding (M), and `tools/flashimg` writes it through the same `journal.c` (`dhara/README.md`) | +106 B over PR 636 (M; `subr_crc32.o` is in all four kernels' `compile/*/Makefile`); about two table lookups per byte over 944 B per checkpoint and per candidate (I) | sound (M: cuts 0-943 and 945-947 prev, 944 and 948-1024 new) | recovered the previous checkpoint for all 2,000 seeds, at 1/2 and 31/32 (M) | unchanged | none required |
| Commit marker in the last chip page (magic moved to bytes 1021-1023) | a prefix tear never shows the magic | breaking: old kernels see no checkpoint | about +10 B (I) | sound for prefix tears (I) | the marker shares a chip program with the root slot (I) | unchanged | reflash every root, or recognize both layouts |
| Marker byte in a second program | program the page, then one marker byte | additive | needs a RAM-resident single-byte page program, because the boot ROM programs 256-byte multiples and a second 256-byte program clocks 0xff over programmed bytes outside the W25Q16JV Page Program contract (C, `dhara-torn-checkpoint.md` "Alternatives"; datasheet "Page Program (02h)"); RAM-resident text of order 100-200 B (I) | sound (I) | the marker can be partly programmed (I) | +1 command per checkpoint | as CRC |
| Header-last program order | program bytes 256-1023, then the header chip page | unchanged | about +20 B (I) | insufficient alone: a prefix of the header chip page leaves the magic with a torn tail, cookie and slots 0-1 (C, `dhara-torn-checkpoint.md` "Alternatives") | insufficient (I) | unchanged | none |
| LittleFS (metadata pairs) | two blocks per metadata pair, commits appended with one CRC per commit; the newer revision of a valid pair wins, and a commit becomes valid only "when the commit's checksum is written" (C, littlefs `DESIGN.md`, "Metadata pairs") | a file system, not a block layer: replaces UFS on the root | 15,330 B text (C, `log-structured-store-scope.md:21-22`, arm-none-eabi-gcc 16.2.0); about 1.7 KiB RAM for one open file (C, same, line 228) | sound by design (C) | sound by design (C) | dynamic only (C, `DESIGN.md` "Wear leveling") | new root and tools; exceeds the text headroom |
| SPIFFS | flat log of pages with flags rewritten in place | own FS, no directories (C, spiffs `README.md`) | not compiled | relies on "repeated programs to NOR flash" being "atomic and masking" (C, littlefs `DESIGN.md`, "Existing designs?"), which the 256-byte boot ROM program path does not provide (I) | as reset | static (C, spiffs `README.md`) | new root |
| JFFS2; NetBSD CHFS | a log of nodes, each with header, node and data CRCs; mount scans all erase blocks and builds the map in RAM (S, netbsd-src 1e8e727e461 `sys/ufs/chfs/media.h:73-140`, `chfs_scan.c:73-124`; JFFS2 node headers read in an Android kernel tree, GPL) | own FS | in-RAM node references per live node, kilobytes for 989 sectors (I); CHFS is 11,086 lines of C (M, `wc -l`) | sound (I) | sound (I) | dynamic | new root |
| UBI (+UBIFS) | per-erase-block erase-counter and volume-id headers, each with a CRC; when two blocks claim one logical block, the newer wins only if its data CRC checks, else the older (S, Android kernel tree 8d6f27ecec2 `drivers/mtd/ubi/ubi-media.h:134-294`, GPL) | volume layer below a FS | erase-block tables in RAM, a few KB for 192 blocks (I); code tens of KB (I) | sound (I) | sound (I) | global erase-count leveling | new root |
| YAFFS2 | per-chunk tags with sequence numbers; a checksummed checkpoint for fast mount, and a full backward scan of the tags when the checkpoint fails (S, same tree, `fs/yaffs2/yaffs_guts.c:4996-5029`, `yaffs_checkptrw.c:192-196`, GPL or commercial) | own FS | per-chunk RAM tables (I) | sound (I) | sound (I) | dynamic | new root |
| NetBSD `flash(4)`, `nand(4)` | block interface with bad-block queries and a write cache; no translation layer (S, `sys/dev/flash/flash.c:418-618`, `flash_io.c`) | none | not applicable | not applicable | not applicable | none | not applicable |
| OpenBSD | no flash or NAND driver directory under `sys/dev` (M, `ls` of openbsd-src) | | | | | | |
| DragonFly HAMMER2 | four volume-header copies, each with per-sector and whole-header CRCs; mount takes the valid copy with the highest transaction id (S, DragonFlyBSD 94448f62 `sys/vfs/hammer2/hammer2_disk.h:1219-1310`, `hammer2_ondisk.c:529-572`) | volume header | not applicable here | sound (I) | sound (I) | not applicable | not applicable |

What carries across:

- **Commit check.** Each design here that claims power-loss safety commits through a check value written with the data: LittleFS, UBI, CHFS, JFFS2 and HAMMER2. Dhara's checkpoint has none. PR 636 derives one from the tree's structure, and in the bit model that structural check rejected every tear. The CRC row adds an explicit check without moving a field, so it is the cheapest fallback if a measurement ever defeats the structural one.
- **Rebuild by scan.** Each design that rebuilds from a scan does so at mount, in RAM sized by the live data. YAFFS2, for example, falls back from a bad checkpoint to a scan. This kernel cannot afford that RAM, but a host can (section 3).

## 3. D26: roots the old kernel already damaged

### What damage looks like (M)

For each class, the harness:

1. Tears a checkpoint under the old journal.
2. Resumes and continues under it: rewrites sectors 0 to 199, adds sectors 700 to 749, and syncs three times (about 38 blocks).
3. Resumes under each candidate rule.

| Cut | Old kernel at first boot | Old kernel continuing | PR 636 | Upstream fallback | Minimum-gap heal | Host scan |
| --- | --- | --- | --- | --- | --- | --- |
| 932 (pointer erased) | 7 sectors blank | works; count 757, nodes 750 | blank | old view, count 757 | old view, count 750 | all 750 at their latest version |
| 936 | 3 blank | count 753, nodes 750 | blank | old view | healed | all 750 |
| 940 | 1 blank | count 751, nodes 750 | blank | old view | healed | all 750 |
| 929 (pointer torn) | 7 blank, 8 EIO (sector 0 among them) | 8 writes fail with EIO; checkpoints continue | blank | old view, 8 sectors EIO | blank (structural) | all 750; the 8 sectors return at their last durable version |
| 937 | 1 blank, 2 EIO | sync fails with EIO; no later checkpoint lands | prev (full) | prev | prev | all 750 |

Further runs (M):

- **The gap is permanent.** Rewriting a lost sector goes through `trace_path()`, which does not find it, so the count rises again (`map.c:354-366`). Collection skips unreachable pages as garbage (`map.c:226-238`).
- **Healing sticks.** After the heal and one more checkpoint, PR 636 alone resumes the root with the count equal to the nodes.
- **Short continuation.** After cut 932 the old kernel wrote and checkpointed only three blocks (sectors 300-309 and 700-704). PR 636 then resumed the checkpoint before the tear. The 7 lost sectors came back, and the 15 acknowledged writes were dropped, with no message.
- **Wrap.** After cut 932 the old kernel rewrote sectors 100 to 199 twenty-five times until the ring wrapped (epochs 3 and 4 on the chip). The scan then matched the old kernel's view exactly, and the 7 lost sectors were gone from the chip.

The classes above are prefix-shaped. A power cut, the only cause that reaches the window on a board, may leave other shapes. In the bit model PR 636 rejected every one, but the model is assumed, not measured on silicon.

### How many roots (I)

Damage needs every one of these:

- **A tear in the bootable band.** Under the reset model a reset must land in the band: at most 14 microseconds for a root id of 1 or more, and 49 microseconds when the root id is 0 (I; the band widths are M, the appendix sweeps). A USB-requested reset cannot land inside a flash request, because the driver masks interrupts across it, and core 1's watchdog cannot fire there either (C, `flash-reset-recovery-cells.md`, "What the board run needs"). That leaves the RUN pin, a debugger or the unbuilt reset hook, and no such campaign is recorded. Under power loss the fourth chip program's self-timed phase is the window: 375 microseconds per page program, measured on the board (C, `flash-traffic-per-workload.md:44-45`). The datasheet's only statement is that a power-off during a suspended program may corrupt its data (C, W25Q16JV datasheet, "Erase / Program Suspend (75h)").
- **An unplug that hits it.** A random unplug hits that window with a probability of about 375 microseconds over the checkpoint period. That is about 1.3 x 10^-5 when update(8) checkpoints a dirty root every 30 s (`STORAGE.md:277`), and less when idle.
- **No reflash since.** A `flash.uf2` reflash rewrites the whole region (`distrib/rp2040/Makefile.inc:138-141`). picotool 2.3.1 keeps erased blocks in the UF2: an 8 KiB all-0xff input became 32 UF2 blocks (M).
- **A kernel-only update onto that root.** `distrib/rp2040/host/DEVELOPMENT.md:173-174` shows a kernel flashed with and without the root.

The recorded board history holds seven USB warm resets and later forced-reset trials, with no power-cut campaign (C, `STORAGE.md:333-344`). The power-loss oracle H09 is blocked (C, `rp2040-memory-wear-engineering-program.md:201`). That history bounds one board, not a deployed population: no count of deployed roots, of their power losses or of their dirty checkpoints is recorded, so the measured history shows no damaged root and the number in service stays unknown (inference). Either of these would show a damaged root:

- a count-node gap found in a `picotool save` dump;
- an old kernel that fails reads with EIO.

### Migration options (M where marked)

| Option | Result on D26 roots | Cost | Risk |
| --- | --- | --- | --- |
| Upstream fallback | mounts the old view (all classes) | +92 B | issue 619 returns for that root at every later checkpoint (M) |
| Minimum-gap heal | erased-pointer class healed; torn-pointer class blank; short continuation still rolls back | +88 B | the first checkpoint after the heal is exposed (M, 3 of 1,025 cuts) |
| Refuse with a message | the root does not mount and the region stays untouched; the short-continuation rollback is not caught, so mount also reports the number of newer checkpoints it rejected | shared with D28: +92 B for the sketch, a lower bound; the report tens of bytes (I) | none to data; owner action needed |
| Host scan (salvage) | every measured class recovered until collection laps the lost pages (M) | 0 B kernel; a host tool | the prototype orders pages by (epoch, block), which mis-orders the split journal of section 4; a real tool follows the journal's own epoch search, and checks a torn newest page with the tree walk before trusting its ids |
| Reflash | data lost unless salvaged first | none | none |

Recommended, in order:

1. Refusal with a message (section 4) and a mount report of rejected newer checkpoints.
2. The host scan as the salvage route, then a reflash of the rebuilt image (`tools/flashimg` turns a sector image into a region).

The heal is not recommended. It is unsound for its first checkpoint, does not cover the torn-pointer class, and changes nothing for the short continuation. It stays an option.

## 4. D28: a failed resume

Failure model (S):

- **Ways resume fails.** It fails in two ways. Either no checkpoint appears in blocks 0 to 7 (`journal.c:231-250`): a fresh or erased region. Or no acceptable checkpoint appears in the search window: D26, more than eight consecutive tears, or corruption.
- **Read errors.** XIP reads cannot fail beyond the range check (`flash.c:692-707`).
- **No writes during resume.** Resume writes nothing (C, `flash-reset-recovery-cells.md`, row `U-resume`).
- **Both failures look alike.** Both set `DHARA_E_TOO_BAD` (`journal.c:249`, `journal.c:367`).

What "print and continue" does today (M):

- On a damaged root (cut 932, then continued use), the PR 636 journal resumes an empty map.
- One write and a sync erase block 0, because `reset_journal()` puts the head at 0 (`journal.c:178-200`, `journal.c:653-684`).
- The next resume still fails. `find_last_checkblock()` follows the old lap's epoch (`journal.c:253-283`), so the new write is lost too.
- Fifty writes erased eight blocks.
- The root case panics before writing (`init_main.c:126-128`), but any other user of `fl0` writes into this split journal.

| Option | Behavior | Cost | Verdict |
| --- | --- | --- | --- |
| Print and continue | empty map; writes destroy old blocks and are themselves lost | a message only (I) | rejected (M above) |
| Print and refuse | `fl_setup()` returns ENXIO with "no whole Dhara checkpoint (error N); root region left untouched", and `find_checkblock()` reports `DHARA_E_NOT_FOUND`, so a fresh region still mounts empty; the not-found exit needs an erased-region test first, because torn headers in blocks 0-7 reach the same exit (C, `journal.c:231-250`) | flash.o 3,437 to 3,529 B (+92, M) for the appendix sketch only, a lower bound: the erased-region test and the rejected-checkpoint report are not built or measured; journal.o unchanged (M); 0 RAM | recommended |
| Rebuild in the kernel | scan metadata into a sector-to-page table and write a new journal | about 2 KB RAM for 989 two-byte entries, plus journal space to copy every live sector (I); PICOCALC has about 10 KB free (C, brief) | rejected; done on the host instead |

Checks of the refusal (M):

- A fresh region resumes with error 6 (`DHARA_E_NOT_FOUND`) under the edited journal, against 3 under PR 636.
- With both edits on the PR 636 tree, `flash_checkpoint/check.sh` and `flash_reset_cells/check.sh` pass. The edits change none of their rows, the fresh-chip scenarios included.
- No oracle row reaches the refusal itself. The refusal as sketched in the appendix is incomplete: the `DHARA_E_NOT_FOUND` exit of `find_checkblock()` is also reached when blocks 0-7 hold torn checkpoint headers (C, `journal.c:231-250`), so a complete refusal first tells an erased region from a torn one (section 4, appendix).

## 5. Recommendation

- **Now:** land PR 636 with the refusal, the `DHARA_E_NOT_FOUND` distinction (reported only for a region whose blocks 0-7 read as erased) and a mount report of rejected newer checkpoints. Make no format change. Write the host salvage scan over a `picotool save` dump, ordered by the journal's own epoch search.
  - Falsifiers: a D26 or torn image that the scan cannot rebuild before collection laps it; a fresh or `tools/flashimg` region that the refusal blocks; a healthy root that resumes differently.
  - Deciding measurement: D26 rows in `check-flash-reset-cells`. Each row damages an image with the old `journal.c`, continues it short and long, and resumes it with the new one. A long continuation expects the refusal, a short one the rollback report, and both a scan that rebuilds the old view. Add a fresh-region case for the refusal.
  - Kernel cost: journal.o +396 B and flash.o +92 B (gcc 16.2.0, objects; the flash.o figure is a lower bound, since the erased-region test and the rejected-checkpoint report are not built), so linked text near +490 B per kernel or more (I, from PR 636's equal object and kernel deltas). RAM: 0 static; resume stack +168 B.
- **Later, gated on a measurement:** the additive CRC-32 in the checkpoint padding, keeping the tree walk for legacy pages. Land it only if H09's power-loss oracle or a board power-cut campaign shows PR 636 accepting a page outside the permitted set.
  - Falsifier of the gate: such a page, which the bit model of this note did not produce in 3,000 tries.
  - Cost: +106 B over PR 636 (M).

## Owner decisions

1. **D26.** Choose one:
   - refusal, the rollback report and host salvage (recommended);
   - the minimum-gap heal (+88 B; one exposed checkpoint; the torn-pointer class is not covered);
   - the upstream fallback (rejected by measurement).
2. **The short-continuation rollback.** Choose between reporting it only (recommended) and refusing when more than one newer checkpoint is rejected. Refusing would also refuse two consecutive legitimate tears.
3. **D28.** Refuse with a message (recommended) or keep today's empty mount.
4. **Tools and rows.** Whether to build the host salvage tool and the D26 oracle rows.
5. **The CRC field.** Whether the CRC waits for its gate, or lands now as a defense that nothing measured requires.
6. **GPL sources.** This audit read GPL sources (the UBI, YAFFS2 and JFFS2 headers of an Android kernel tree) for mechanism only. No text or layout of theirs is copied here. The recommended designs rest on PR 636 and on BSD-licensed precedent: LittleFS (BSD 3-Clause), CHFS and HAMMER2. The owner decides whether a later implementation needs an author who has not read the GPL sources, as `flash.c` did for FUZIX.

## Appendix: reproduction

The harness links `error.c`, `map.c` and one `journal.c` over a
192-block NOR model:

- A program is AND-only and counts any program over unerased bytes as a fault.
- `tear:K` makes the next checkpoint-page program keep its first K bytes, then save the image and exit 3. That is the reset.
- `ptear:S` programs the first 768 bytes in full and a seeded subset of the zero bits in the last 256. Seeds from 100,000 program about 7/8 of those bits, and seeds from 200,000 about 31/32.
- The other commands run left to right: `img=FILE`, `fresh`, `load`, `save`, `resume`, `w:LO-HI:VERSION`, `sync`, `stats` (erases, programs, faults and failed writes), `check:N` (the version each sector reads; 0 blank, -9 a read error) and `scan:N`.
- Every continuation that printed `stats` reported zero faults.

Setup. Run from the root of a fresh checkout of this branch, in a POSIX
shell, with a C compiler as `cc`. The old journal is the vendored
`journal.c` (identical to `origin/main` f4066c03 at the time of writing).
PR 636's journal comes from its pull-request ref, pinned by commit. The
harness and the sweep script are extracted from the two headed code blocks
below, and the CRC variant is the diff in its own section below, applied to
PR 636's file:

    T=$(mktemp -d)
    D=sys/arch/rp2040/dhara
    N=docs/research/dhara-audit.md
    extract() { awk -v h="### $1" '$0 == h { f = 1; next } f && /^```/ { if (g) exit; g = 1; next } g' "$N"; }
    extract harness.c > "$T/harness.c"
    extract sweep.sh > "$T/sweep.sh"
    git fetch origin refs/pull/636/head
    git show c439e459:$D/journal.c > "$T/journal636.c"
    extract crc.diff > "$T/crc.diff"
    cp "$T/journal636.c" "$T/journalcrc.c"
    patch -l "$T/journalcrc.c" < "$T/crc.diff"
    sed '/^#include/d' sys/kern/subr_crc32.c > "$T/crc32.c"
    cc -std=c17 -O1 -Wall -Wextra -Werror -iquote $D -o "$T/h_old" "$T/harness.c" \
        $D/error.c $D/journal.c $D/map.c
    cc -std=c17 -O1 -Wall -Wextra -Werror -iquote $D -o "$T/h_636" "$T/harness.c" \
        $D/error.c "$T/journal636.c" $D/map.c
    cc -std=c17 -O1 -Wall -Wextra -Werror -iquote $D -o "$T/h_crc" "$T/harness.c" \
        $D/error.c "$T/journalcrc.c" $D/map.c "$T/crc32.c"

Cut sweeps (K runs from 0 to 1024 inside `sweep.sh`; the two numbers are the
first and last sector rewritten at version 2 before the cut checkpoint). The
rewritten range picks the root sector id, because the last page of the group
is a garbage-collection copy: `0 9` gives id 13, `600 609` id 3, `1 6` id 0
and `0 599` id 601 under the old journal (M; the id is the first four bytes
of slot 6 of the checkpoint page, byte 812 of the page):

    for v in old 636 crc; do
        for r in "0 9" "600 609" "1 6" "0 599"; do
            echo "== $v rewrite $r"
            sh "$T/sweep.sh" "$T/h_$v" "$T" $r
        done
    done

One cut by hand, and a read of the result (K is a number, here 932):

    "$T/h_old" img="$T/t.img" fresh resume w:0-699:1 sync w:0-9:2 tear:932 sync   # exits 3
    "$T/h_old" img="$T/t.img" load resume check:700

A D26 image continued under the old journal, then resumed under PR 636:

    "$T/h_old" img="$T/t.img" load resume w:0-99:3 sync w:100-199:3 sync w:700-749:1 sync save
    "$T/h_636" img="$T/t.img" load resume check:750

`sweep.sh` labels a cut "prev" when its sector vector equals the one of cut 0
(no magic, so the previous checkpoint), "new" when it equals the one of cut
1024, and "damaged" otherwise. `bad` counts the sectors among 0 to 699 that
read blank (0), unreadable (-9) or garbage (-1). A harness exit other than 3
stops the sweep.

Recorded output (M; GCC 16.2.1, harness commit of this note). The run is
the loop above, in a fresh worktree, with the extracted files:

    == old rewrite 0 9
    0-3 prev bad=0..0
    4-815 damaged sector0=lost, all 700 sectors bad bad=700..700
    816-931 damaged sector0=lost bad=15..699
    932-943 damaged sector0=readable bad=1..7
    944-1024 new bad=0..0
    == old rewrite 600 609
    0-3 prev bad=0..0
    4-815 damaged sector0=lost, all 700 sectors bad bad=700..700
    816-939 damaged sector0=lost bad=3..699
    940-943 damaged sector0=readable bad=1..1
    944-1024 new bad=0..0
    == old rewrite 1 6
    0-3 prev bad=0..0
    4-815 damaged sector0=lost, all 700 sectors bad bad=700..700
    816-943 damaged sector0=readable bad=1..699
    944-1024 new bad=0..0
    == old rewrite 0 599
    0-3 prev bad=0..0
    4-815 damaged sector0=lost, all 700 sectors bad bad=700..700
    816-907 damaged sector0=lost bad=699..699
    908-943 damaged sector0=readable bad=1..187
    944-1024 new bad=0..0
    == 636 rewrite 0 9
    0-943 prev bad=0..0
    944-1024 new bad=0..0
    == 636 rewrite 600 609
    0-943 prev bad=0..0
    944-1024 new bad=0..0
    == 636 rewrite 1 6
    0-943 prev bad=0..0
    944-1024 new bad=0..0
    == 636 rewrite 0 599
    0-943 prev bad=0..0
    944-1024 new bad=0..0
    == crc rewrite 0 9
    0-943 prev bad=0..0
    944-944 new bad=0..0
    945-947 prev bad=0..0
    948-1024 new bad=0..0
    == crc rewrite 600 609
    0-943 prev bad=0..0
    944-944 new bad=0..0
    945-947 prev bad=0..0
    948-1024 new bad=0..0
    == crc rewrite 1 6
    0-943 prev bad=0..0
    944-944 new bad=0..0
    945-947 prev bad=0..0
    948-1024 new bad=0..0
    == crc rewrite 0 599
    0-943 prev bad=0..0
    944-944 new bad=0..0
    945-947 prev bad=0..0
    948-1024 new bad=0..0
    # by hand, cut 932: torn page 815 at 932; resume rc 0 err 0 count 700 root 814
    # check:700 -> sectors 0-7 read 2, sectors 8-12, 14 and 15 read 0, the rest 1; nodes 693 count 700
    # D26 continuation: PR 636 prints resume rc -1 err 3 count 0; check:750 reads 0 everywhere, nodes 0 count 0

Oracles, with `PYTHON=python3`, all exit 0:

- `tests/rp2040/flash_reset_cells/check.sh` and `tests/rp2040/flash_checkpoint/check.sh` on the PR 636 tree.
- The same two on that tree with the two D28 edits below.

Reproducibility of the variants. The old journal, PR 636 and CRC rows of the
cut tables reproduce from the commands above. The minimum-gap heal diff and
the D28 diff are sketches. The heal diff elides hunks, and the D28 diff edits
`flash.c`, which the host harness does not link. The heal figures (+88 B, stack
264 B, the cut 932, 936 and 940 losses) and the D28 figures (flash.o +92 B, error 6
against 3) therefore come from builds that this note does not carry (open:
publish the heal and D28 variants as branches before an owner decision rests
on them).

Object sizes: the PICO compile lines from `bmake -n journal.o flash.o`,
with `machine/` pointed at `sys/arch/rp2040/include`, then
`arm-none-eabi-size`; stack from the same lines with `-fstack-usage`.

### harness.c

```c
/* Host experiment for the Dhara audit: a NOR model of the RP2040 root region
 * (1 KiB Dhara pages, 8 KiB blocks, 192 blocks) under the vendored Dhara map,
 * with a torn checkpoint-page program (byte prefix k) and a metadata scan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "map.h"
#include "nand.h"

#define PAGE 1024
#define PPB 8
#define NBLK 192
#define NPAGE (NBLK * PPB)
static uint8_t region[NBLK * PPB * PAGE];
static const char *image = "chip.img";
static int faults;
static long tear_k = -1;	/* armed: next checkpoint page keeps k bytes */
static long ptear_seed = -1;	/* armed: last chip page of the next checkpoint page gets a random subset of its zero bits */
static long erases, progs, wfail;

const struct dhara_nand nand = { 10, 3, NBLK };

static void save(void)
{
	FILE *f = fopen(image, "wb");
	if (!f || fwrite(region, 1, sizeof(region), f) != sizeof(region)) { perror(image); exit(9); }
	fclose(f);
}
static void load(void)
{
	FILE *f = fopen(image, "rb");
	if (!f || fread(region, 1, sizeof(region), f) != sizeof(region)) { perror(image); exit(9); }
	fclose(f);
}
int dhara_nand_is_bad(const struct dhara_nand *n, dhara_block_t b) { (void)n; (void)b; return 0; }
void dhara_nand_mark_bad(const struct dhara_nand *n, dhara_block_t b) { (void)n; (void)b; }
int dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b, dhara_error_t *err)
{
	(void)n; (void)err;
	memset(region + (size_t)b * PPB * PAGE, 0xff, PPB * PAGE);
	erases++;
	return 0;
}
static void prog_bytes(dhara_page_t p, const uint8_t *d, size_t len)
{
	uint8_t *q = region + (size_t)p * PAGE;
	for (size_t i = 0; i < len; i++) {
		if (q[i] != 0xff) faults++;
		q[i] &= d[i];
	}
}
int dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p, const uint8_t *data, dhara_error_t *err)
{
	(void)n; (void)err;
	progs++;
	if (ptear_seed >= 0 && (p & (PPB - 1)) == PPB - 1) {
		uint8_t *q = region + (size_t)p * PAGE;
		uint32_t x = (uint32_t)ptear_seed * 2654435761u + 12345u;
		prog_bytes(p, data, 768);
		for (size_t i = 768; i < PAGE; i++) {
			x = x * 1103515245u + 12345u;
			uint8_t r = (uint8_t)(x >> 16);
			/* seeds from 100000 program about 7/8 of the zero bits,
			 * from 200000 about 31/32 */
			for (int extra = ptear_seed >= 200000 ? 4 : ptear_seed >= 100000 ? 2 : 0; extra > 0; extra--) {
				x = x * 1103515245u + 12345u;
				r |= (uint8_t)(x >> 16);
			}
			q[i] &= (uint8_t)~((uint8_t)~data[i] & r);
		}
		save();
		printf("partial page %u seed %ld\n", (unsigned)p, ptear_seed);
		exit(3);
	}
	if (tear_k >= 0 && (p & (PPB - 1)) == PPB - 1) {
		prog_bytes(p, data, (size_t)tear_k);
		save();
		printf("torn page %u at %ld\n", (unsigned)p, tear_k);
		exit(3);
	}
	prog_bytes(p, data, PAGE);
	return 0;
}
int dhara_nand_is_free(const struct dhara_nand *n, dhara_page_t p)
{
	(void)n;
	for (size_t i = 0; i < PAGE; i++) if (region[(size_t)p * PAGE + i] != 0xff) return 0;
	return 1;
}
int dhara_nand_read(const struct dhara_nand *n, dhara_page_t p, size_t off, size_t len, uint8_t *data, dhara_error_t *err)
{
	(void)n;
	if (p >= NPAGE || off + len > PAGE) { dhara_set_error(err, DHARA_E_ECC); return -1; }
	memcpy(data, region + (size_t)p * PAGE + off, len);
	return 0;
}
int dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src, dhara_page_t dst, dhara_error_t *err)
{
	uint8_t b[PAGE];
	if (dhara_nand_read(n, src, 0, PAGE, b, err) < 0) return -1;
	prog_bytes(dst, b, PAGE);
	progs++;
	return 0;
}

static struct dhara_map map;
static uint8_t page_buf[PAGE];

static void payload(uint8_t *d, uint32_t s, uint32_t v)
{
	for (int i = 0; i < PAGE; i++) d[i] = (uint8_t)(s * 7 + v * 13 + i);
	memcpy(d, &s, 4); memcpy(d + 4, &v, 4);
}
/* The version a page holds for sector s: 0 blank, -1 garbage, else v. */
static long version_of(const uint8_t *d, uint32_t s)
{
	uint32_t s2, v; uint8_t e[PAGE];
	int blank = 1;
	for (int i = 0; i < PAGE; i++) if (d[i] != 0xff) { blank = 0; break; }
	if (blank) return 0;
	memcpy(&s2, d, 4); memcpy(&v, d + 4, 4);
	payload(e, s, v);
	if (s2 != s || memcmp(d, e, PAGE)) return -1;
	return v;
}
static uint32_t r32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

/* Scan: every page that carries the magic and an in-range tail, newest by
 * (epoch distance from the newest epoch seen, block); per slot an id whose
 * high byte is programmed maps that user page. */
static void scan(uint32_t nsec)
{
	static uint32_t best_page[4096]; static long best_key[4096];
	long newest_epoch = -1; int newest_blk = -1;
	for (uint32_t s = 0; s < nsec; s++) { best_page[s] = 0xffffffff; best_key[s] = -1; }
	/* The newest checkpoint fixes epoch order: blocks after it belong to
	 * the previous lap. Find the block of the highest key. */
	for (int b = 0; b < NBLK; b++) {
		const uint8_t *m = region + ((size_t)b * PPB + PPB - 1) * PAGE;
		if (m[0] != 'D' || m[1] != 'h' || m[2] != 'a' || r32(m + 4) >= NPAGE) continue;
		long e = m[3];
		if (e > newest_epoch || (e == newest_epoch && b > newest_blk)) { newest_epoch = e; newest_blk = b; }
	}
	for (int b = 0; b < NBLK; b++) {
		const uint8_t *m = region + ((size_t)b * PPB + PPB - 1) * PAGE;
		if (m[0] != 'D' || m[1] != 'h' || m[2] != 'a' || r32(m + 4) >= NPAGE) continue;
		long key = (long)m[3] * NBLK + b;
		for (int i = 0; i < PPB - 1; i++) {
			uint32_t id = r32(m + 20 + i * 132);
			if ((id >> 24) == 0xff || id >= nsec) continue;
			if (key > best_key[id]) { best_key[id] = key; best_page[id] = (uint32_t)(b * PPB + i); }
		}
	}
	printf("scan newest epoch %ld block %d\n", newest_epoch, newest_blk);
	printf("scanmap");
	for (uint32_t s = 0; s < nsec; s++) {
		long v = best_page[s] == 0xffffffff ? 0 : version_of(region + (size_t)best_page[s] * PAGE, s);
		printf(" %ld", v);
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	dhara_error_t err = DHARA_E_NONE;
	uint8_t d[PAGE];
	for (int a = 1; a < argc; a++) {
		const char *c = argv[a];
		unsigned lo, hi, v; long k;
		if (!strcmp(c, "fresh")) { memset(region, 0xff, sizeof(region)); }
		else if (!strncmp(c, "img=", 4)) { image = c + 4; }
		else if (!strcmp(c, "load")) { load(); }
		else if (!strcmp(c, "save")) { save(); }
		else if (!strcmp(c, "resume")) {
			dhara_map_init(&map, &nand, page_buf, 4);
			err = DHARA_E_NONE;
			int rc = dhara_map_resume(&map, &err);
			printf("resume rc %d err %d count %u root %u\n", rc, (int)err,
			    (unsigned)map.count, (unsigned)dhara_journal_root(&map.journal));
		} else if (sscanf(c, "w:%u-%u:%u", &lo, &hi, &v) == 3) {
			for (unsigned s = lo; s <= hi; s++) {
				payload(d, s, v);
				if (dhara_map_write(&map, s, d, &err) < 0) { wfail++; if (wfail <= 3) printf("write %u failed err %d\n", s, (int)err); }
			}
		} else if (!strcmp(c, "sync")) {
			if (dhara_map_sync(&map, &err) < 0) { printf("sync failed err %d\n", (int)err); return 4; }
		} else if (sscanf(c, "tear:%ld", &k) == 1) { tear_k = k; }
		else if (sscanf(c, "ptear:%ld", &k) == 1) { ptear_seed = k; }
		else if (sscanf(c, "check:%u", &hi) == 1) {
			unsigned found = 0;
			printf("check");
			for (unsigned s = 0; s < hi; s++) {
				dhara_page_t p;
				dhara_error_t e2;
				if (dhara_map_read(&map, s, d, &e2) < 0) { printf(" -9"); continue; }
				if (dhara_map_find(&map, s, &p, &e2) == 0) found++;
				printf(" %ld", version_of(d, s));
			}
			printf("\nnodes %u count %u\n", found, (unsigned)map.count);
		} else if (sscanf(c, "scan:%u", &hi) == 1) { scan(hi); }
		else if (!strcmp(c, "stats")) { printf("stats erases %ld progs %ld faults %d wfail %ld\n", erases, progs, faults, wfail); }
		else { fprintf(stderr, "bad command %s\n", c); return 2; }
	}
	return faults ? 5 : 0;
}
```

Two defects of this sketch bind the salvage tool (owner decision D47) and each needs an oracle row before the tool counts. Two writes of one sector inside one checkpoint group share the key `(epoch, block)`, and the strict `key > best_key` comparison keeps the first, stale slot; the order must include the slot position, newest last. The key is numeric, so after the 8-bit epoch wraps from 255 to 0 the stale epoch-255 pages outrank the current ones; `newest_epoch` is computed but unused, and the order must be the modular distance from the newest accepted checkpoint (I, from the code above; no oracle row yet). The CRC sketch below also keeps `cp_tree_whole()` for every page, since the CRC covers only the checkpoint page and not the metadata pages its pointers reach.

### sweep.sh

```sh
#!/bin/sh
# usage: sweep.sh HARNESS WORKDIR LO HI
# Cuts the checkpoint page that follows a rewrite of sectors LO..HI at every
# byte K from 0 to 1024, resumes, reads sectors 0-699, and prints one line per
# run of consecutive cuts that leave the same class of state.
set -eu
h=$1 w=$2 lo=$3 hi=$4
: > "$w/raw.txt"
k=0
while [ "$k" -le 1024 ]; do
	rc=0
	"$h" img="$w/t.img" fresh resume w:0-699:1 sync "w:$lo-$hi:2" "tear:$k" sync >/dev/null || rc=$?
	if [ "$rc" -ne 3 ]; then
		echo "cut $k: harness exit $rc, expected 3" >&2
		exit 1
	fi
	echo "$k $("$h" img="$w/t.img" load resume check:700 | tr '\n' ' ')" >> "$w/raw.txt"
	k=$((k + 1))
done
awk '
function flush() {
	if (first >= 0)
		printf "%d-%d %s\n", first, last, label (bmin >= 0 ? sprintf(" bad=%d..%d", bmin, bmax) : "")
}
{
	row[NR] = $0
	vec = ""
	if ($0 ~ / resume rc 0 /) {
		for (i = 1; i <= NF; i++) if ($i == "check") break
		for (s = 0; s < 700; s++) vec = vec " " $(i + 1 + s)
	}
	v[NR] = vec
	key[NR] = $1
}
END {
	prevv = v[1]
	newv = v[NR]
	first = -1
	for (n = 1; n <= NR; n++) {
		split(v[n], x, " ")
		bad = -1
		if (v[n] == "") cls = "no-resume"
		else {
			bad = 0
			for (s = 1; s <= 700; s++) if (x[s] == 0 || x[s] == -9 || x[s] == -1) bad++
			if (v[n] == prevv) cls = "prev"
			else if (v[n] == newv) cls = "new"
			else cls = "damaged sector0=" ((x[1] == 1 || x[1] == 2) ? "readable" : "lost") (bad == 700 ? ", all 700 sectors bad" : "")
		}
		if (cls != label) { flush(); first = key[n]; label = cls; bmin = -1; bmax = -1 }
		last = key[n]
		if (bad >= 0) { if (bmin < 0 || bad < bmin) bmin = bad; if (bad > bmax) bmax = bad }
	}
	flush()
}
' "$w/raw.txt"
```

### Minimum-gap heal (with an optional upstream fallback), against PR 636

```diff
--- a/sys/arch/rp2040/dhara/journal.c
+++ b/sys/arch/rp2040/dhara/journal.c
@@ -413,11 +413,12 @@
        return 0;
 }

-static int cp_tree_whole(const struct dhara_journal *j, dhara_page_t root)
+/* Nodes the tree rooted at root reaches, or -1 when an id has its high byte
+ * erased or an alt pointer names no user page. */
+static long cp_tree_seen(const struct dhara_journal *j, dhara_page_t root)
 {
        const uint32_t pages =
                (uint32_t)j->nand->num_blocks << j->nand->log2_ppb;
-       const uint32_t count = dhara_r32(j->page_buf + DHARA_HEADER_SIZE);
        dhara_page_t page[CP_RADIX_DEPTH + 1];
        uint8_t level[CP_RADIX_DEPTH + 1];
        dhara_page_t next = root;
@@ -427,41 +428,32 @@
        int top = -1;

        if (meta_field(j, root, 0, &v) < 0)
-               return 0;
+               return -1;
        if (v == CP_ID_ERASED)
-               return count == 0;
-       if (count > pages)
                return 0;
-
        for (;;) {
                if (next != DHARA_PAGE_NONE) {
                        if (meta_field(j, next, 0, &v) < 0 || (v >> 24) == 0xff ||
-                           ++seen > count)
-                               return 0;
+                           ++seen > pages)
+                               return -1;
                        top++;
                        page[top] = next;
                        level[top] = next_level;
                        next = DHARA_PAGE_NONE;
                }
-
                if (top < 0)
                        break;
-
                if (level[top] >= CP_RADIX_DEPTH) {
                        top--;
                        continue;
                }
-
                next_level = level[top]++;
-               if (meta_field(j, page[top], 4 + ((size_t)next_level << 2),
-                              &v) < 0)
-                       return 0;
-
+               if (meta_field(j, page[top], 4 + ((size_t)next_level << 2), &v) < 0)
+                       return -1;
                next = v;
                next_level++;
        }
-
-       return seen == count;
+       return (long)seen;
 }

 /* DiscoBSD local change: the search steps back past a checkpoint that
@@ -481,6 +473,8 @@
        int i = (start & ((1 << j->nand->log2_ppb) - 1)) >> j->log2_ppc;
        uint8_t epoch = j->epoch;
        int blocks = 0;
+       dhara_page_t best = DHARA_PAGE_NONE;
+       long best_gap = -1, best_seen = 0;

        while (i >= 0) {
                const dhara_page_t p = (blk << j->nand->log2_ppb) +
@@ -490,10 +484,21 @@
                                     0, 1 << j->nand->log2_page_size,
                                     j->page_buf, err) &&
                    hdr_is_checkpoint(j, j->page_buf) &&
-                   (hdr_get_epoch(j->page_buf) == epoch) &&
-                   cp_tree_whole(j, p - 1)) {
-                       j->root = p - 1;
-                       return 0;
+                   (hdr_get_epoch(j->page_buf) == epoch)) {
+                       const long count = (long)dhara_r32(j->page_buf +
+                           DHARA_HEADER_SIZE);
+                       const long seen = cp_tree_seen(j, p - 1);
+
+                       if (seen >= 0 && seen == count) {
+                               j->root = p - 1;
+                               return 0;
+                       }
+                       if (seen > 0 && seen < count &&
+                           (best_gap < 0 || count - seen < best_gap)) {
+                               best_gap = count - seen;
+                               best_seen = seen;
+                               best = p;
+                       }
                }

                i--;
@@ -507,6 +512,34 @@
                }
        }

+       if (best != DHARA_PAGE_NONE &&
+           !dhara_nand_read(j->nand, best, 0, 1 << j->nand->log2_page_size,
+                            j->page_buf, err)) {
+               dhara_w32(j->page_buf + DHARA_HEADER_SIZE, (uint32_t)best_seen);
+               j->root = best - 1;
+               return 0;
+       }
+#if AUDIT_FALLBACK
+       {
+               const dhara_block_t b0 = start >> j->nand->log2_ppb;
+               int k = (start & ((1 << j->nand->log2_ppb) - 1)) >> j->log2_ppc;
+
+               for (; k >= 0; k--) {
+                       const dhara_page_t p = (b0 << j->nand->log2_ppb) +
+                               ((k + 1) << j->log2_ppc) - 1;
+
+                       if (!dhara_nand_read(j->nand, p, 0,
+                                            1 << j->nand->log2_page_size,
+                                            j->page_buf, err) &&
+                           hdr_is_checkpoint(j, j->page_buf) &&
+                           hdr_get_epoch(j->page_buf) == j->epoch) {
+                               j->root = p - 1;
+                               return 0;
+                       }
+               }
+       }
+#endif
+
        dhara_set_error(err, DHARA_E_TOO_BAD);
        return -1;
 }
```

### crc.diff

The CRC-32 rule against PR 636 (c439e459), written with spaces for the source's tabs; apply it with `patch -l`. `crc32_update()` is the kernel's `sys/kern/subr_crc32.c` without its include line. An erased CRC field falls back to the tree walk.

```diff
--- a/sys/arch/rp2040/dhara/journal.c
+++ b/sys/arch/rp2040/dhara/journal.c
@@ -18,6 +18,9 @@
 #include "journal.h"
 #include "bytes.h"

+unsigned crc32_update(unsigned, const void *, unsigned);
+#define CP_CRC_OFF      944
+
 /************************************************************************
  * Metapage binary format
  */
@@ -491,7 +494,10 @@
                                      j->page_buf, err) &&
                     hdr_is_checkpoint(j, j->page_buf) &&
                     (hdr_get_epoch(j->page_buf) == epoch) &&
-                    cp_tree_whole(j, p - 1)) {
+                    cp_tree_whole(j, p - 1) &&
+                    (dhara_r32(j->page_buf + CP_CRC_OFF) == 0xffffffffU ||
+                     dhara_r32(j->page_buf + CP_CRC_OFF) ==
+                     crc32_update(0xffffffffU, j->page_buf, CP_CRC_OFF))) {
                         j->root = p - 1;
                         return 0;
                 }
@@ -982,6 +988,8 @@
         hdr_set_tail(j->page_buf, j->tail);
         hdr_set_bb_current(j->page_buf, j->bb_current);
         hdr_set_bb_last(j->page_buf, j->bb_last);
+        dhara_w32(j->page_buf + CP_CRC_OFF,
+                  crc32_update(0xffffffffU, j->page_buf, CP_CRC_OFF));

         if (dhara_nand_prog(j->nand, j->head + 1, j->page_buf, &my_err) < 0)
                 return recover_from(j, my_err, err);
```

### D28 refusal, against PR 636 and `main`

This sketch is incomplete (C, `journal.c:231-250`: each probe reads only the magic of a block's first checkpoint page, for at most `DHARA_MAX_RETRIES` blocks). `find_checkblock()` also reaches its final exit when
blocks 0-7 hold programmed but invalid checkpoint pages (for example eight torn
checkpoint headers) while later blocks keep recoverable checkpoints, so
returning `DHARA_E_NOT_FOUND` there classifies that damaged region as fresh
and reopens the destructive empty mount. A complete refusal separates an
erased region (every page of blocks 0-7 reads as erased) from one with
programmed but unacceptable pages, and gains a known-bad oracle row for that
case before it lands.

```diff
--- a/sys/arch/rp2040/dhara/journal.c
+++ b/sys/arch/rp2040/dhara/journal.c
@@ -264,7 +264,7 @@
                blk++;
        }

-       dhara_set_error(err, DHARA_E_TOO_BAD);
+       dhara_set_error(err, DHARA_E_NOT_FOUND);
        return -1;
 }
--- a/sys/arch/rp2040/dev/flash.c
+++ b/sys/arch/rp2040/dev/flash.c
@@ -874,7 +874,12 @@
                 * reports by returning -1 after initializing an empty map.
                 * That is the newly-formatted case rather than a failure.
                 */
-               (void)dhara_map_resume(&flmap, &err);
+               if (dhara_map_resume(&flmap, &err) < 0 &&
+                   err != DHARA_E_NOT_FOUND) {
+                       printf("fl%d: no whole Dhara checkpoint (error %d); "
+                           "root region left untouched\n", unit, (int)err);
+                       return ENXIO;
+               }
                flblocks = (daddr_t)dhara_map_capacity(&flmap) /
                    (DEV_BSIZE / FLASH_UNIT_BYTES);
                flmap_ready = 1;
```
