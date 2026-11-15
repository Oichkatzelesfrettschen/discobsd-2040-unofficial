# Scope of a log-structured store for the RP2040 port

Evidence class: sources read, one local kernel build, and compile-only
measurements. No board, no Renode run, no mounted-card measurement exists
behind any number here. Tree: built and read at 270422ca. `origin/main` has
since advanced to a849d47a (PR #548); `git diff --stat 270422ca origin/main`
shows tool, test and documentation changes and no change under `sys/` code, so
the kernel figures stand (M). Every number carries a tag:
M (measured here, command named, or read from this tree's source where the
tag says "by reading"), C (cited from a named source), D (derived
by arithmetic shown), E (estimate or inference, stated as such).

## Finding

No log-structured file system should be built for this port now. Three
measured facts decide it, and one cited fact redirects the SD work.

1. The kernels that carry the SD driver have no room. PICOCALC and
   PICOCALC_PSRAM leave 11,408 and 8,712 bytes of kernel text headroom (M)
   and about 11.1 KB and 10.6 KB of unassigned SRAM below the u-areas (M, D).
   A LittleFS-class store, the one candidate compiled here, compiles to
   15,330 bytes of text before any block glue or kernel adapter (M).
2. The root already sits on a log. Dhara is a log-structured translation
   layer with its own journal and cleaner. A second log above it multiplies
   write amplification and needs a trim call per cleaned segment that the
   kernel never issues (section 3).
3. The SD driver is single-block and issues no erase. The SD Physical Layer
   specification v6.00 states that Discard and FULE are not supported in SPI
   mode, that only a plain CMD38 erase exists there, and that the host treats
   the card as Class 0 (section 3, C and M). A plan that relies on a discard
   argument in SPI mode has no support in the version read.

The cheap, measurable parts carry further than any new format: the
inode-before-free order is a prerequisite for any erase on either medium, an
AU-aligned partition costs nothing, and an erase-cost measurement on a real
card decides whether anything larger is warranted (section 7).

## 1. F2FS mechanisms and their costs

Sources: the FAST 2015 paper (P) and the kernel's `f2fs.rst` (D-doc), both
fetched 2026-11-15. P and D-doc give no entry sizes or RAM costs. The byte
sizes below were taken from the kernel f2fs header and f2fs-tools
at kernel commit 23c62304dde2 and f2fs-tools commit 83fd39a184df. They enter
this note only as totals (per-entry bytes, entries per block), with no record
layout. The licenses of those trees are in section 4 and bind the owner's
decision on that read; see Open questions.

| Mechanism | What it does (own words) | On media | In RAM |
| --- | --- | --- | --- |
| Checkpoint pair | Two alternating packs in two fixed segments; the newest pack whose header and footer versions agree wins at mount (P 2.1, 2.7.1) | 2 segments; one pack is a few blocks (C, kernel source) | one header block; two version bitmaps |
| Segment information table (SIT) | One record per segment: live-block count, a bit per block, last-modified time (P 2.5) | 74 B per record, 55 per 4 KiB block, two alternating copies (C, source) | the kernel keeps all of it resident, about 208 B per segment (C, source); a minimal design pages it |
| Node address table (NAT) | Maps node id to block address, so a data update rewrites one direct node and one table entry, not the index tree (P 2.2) | 9 B per entry, 455 per block, two copies (C, source) | demand-cached |
| NAT/SIT journal | Recent table changes ride in the checkpoint's summary blocks instead of rewriting a table block (P 2.7.1) | about 38 NAT or 6 SIT entries per summary block (D) | resident |
| Segment summary area | Owner of each block (parent node, offset), read by the cleaner (P 2.1, 2.5) | 7 B per block, one 4 KiB block per segment (C, source) | open logs only |
| Node/data separation | Inode, direct and indirect nodes never share a section with data (P 2.1, 2.2) | no extra | none |
| Six logs | Hot, warm, cold node and hot, warm, cold data heads; default 6, 4 or 2 selectable (P 2.4, D-doc). P 3.2.3 reports 4 versus 2 logs as "rather insignificant" | six open segments | six summary buffers |
| Threaded logging | When clean sections run short, write into holes of dirty segments instead of cleaning first (P 2.6); the paper's threshold is 5 percent clean sections, the current kernel uses a formula | none | a second valid-bit set per segment |
| Roll-forward | fsync writes the file's data and its direct node with a mark and no checkpoint; mount replays marked nodes written after the last checkpoint (P 2.7.2) | none | none |
| Cleaning | Greedy victim for foreground, cost-benefit (live count and age) for background; lazy background moves (P 2.5) | reserve of about 5 percent (P, D-doc) | scan state |
| Discard | A cleaned section becomes prefree and is released only after the next checkpoint commits; discard follows (P 2.5, 3.2.1) | none | prefree bitmap |

P and D-doc disagree with the current kernel on the threaded-logging trigger,
the cleaning unit, the background trigger and the discard unit (C, read
from both). The segment is 512 blocks of 4 KiB, 2 MiB, fixed by the
64-byte bitmap (D-doc On-disk Layout); the kernel's minimum block size is
4 KiB, so a 1 KiB-block F2FS does not exist. Paper utilization results
(P 3.2.4, 3.2.5, a 120 GB partition of a 250 GB SATA SSD): about 30 percent loss at 97.5 percent
full, and adaptive logging reduces the loss at 94 percent full from 48 to
22 percent.

## 2. Scale to the two media

All rows are D from the entry sizes in section 1; the 989 KiB row is a
hypothetical design (1 KiB blocks, 16-block segments), not an existing
format, so its entry sizes (12 B SIT, 9 B NAT) are assumptions.

| Volume | Segments | SIT (2 copies) | NAT (2 copies) | SSA | Checkpoint | Metadata |
| --- | ---: | --- | --- | --- | --- | --- |
| 989 KiB, 16-block segments | 61 | 2 blocks | 18 blocks | 6.8 KiB dense (989 x 7 B) | 2 segments | 7 segments, 112 KiB, 11.3 percent (dense SSA); 10 segments, 16.2 percent with one summary block per segment |
| 1 GiB, 2 MiB segments | 512 | 80 KiB | 4.5 MiB | 2 MiB | 4 MiB | 14.5 to 20 MiB by the alignment rule applied (unaligned NAT; each copy rounded to a segment), 1.4 to 2.0 percent |
| 8 GiB | 4,096 | 600 KiB | 36 MiB | 16 MiB | 4 MiB | 60 MiB, 0.73 percent |
| 32 GiB | 16,384 | 2.3 MiB | 144 MiB | 64 MiB | 4 MiB | 216 MiB, 0.66 percent |

Resident RAM, D. A kernel-style resident SIT costs 104 KiB, 832 KiB and
3.25 MiB at 1, 8 and 32 GiB. A paged minimum needs the checkpoint header, one
summary buffer per open log, the version bitmaps (2 B at 989 KiB, 64 B and
2.3 KB at 32 GiB) and one block of each of the five table kinds the cleaner
touches (SIT, NAT, summary, node, data), in blocks of the format's size:

| Geometry | blocks resident | block size | RAM |
| --- | ---: | ---: | ---: |
| 989 KiB hypothetical, 2 logs | 1 + 2 + 5 = 8 | 1 KiB | about 8 to 9 KiB with bitmaps |
| 989 KiB hypothetical, 6 logs | 1 + 6 + 5 = 12 | 1 KiB | about 13 KiB |
| SD with the real F2FS geometry, 2 logs | 8 | 4 KiB | about 32 KiB plus 2.3 KB bitmaps at 32 GiB |

The hypothetical 2-log row leaves 1.6 to 3 KB of the 10.6 to 11.1 KB free on
the SD kernels and nothing for a file system's own tables; the real-geometry
row exceeds the free SRAM of the PICO kernel (23.2 KB) and of both SD
kernels, and fits only PICO_UART (39.9 KB), which has no SD driver. Six open
logs hold 96 blocks, 9.7 percent of the 989-block volume.

The measured budget (M, `bmake MACHINE=rp2040 kernel`, `arm-none-eabi-size`,
`tools/check_kernel_headroom.py` at 270422ca, gcc 16.2.0):

| Kernel | text | headroom (1024 margin) | SRAM free below u-areas (D from section headers) |
| --- | ---: | ---: | ---: |
| PICO | 100,395 | 30,176 | 23,176 |
| PICO_UART | 90,045 | 40,728 | 39,928 |
| PICOCALC (NMOUNT=2) | 119,267 | 11,408 | 11,136 |
| PICOCALC_PSRAM (NMOUNT=2) | 121,667 | 8,712 | 10,568 |

Earlier dated figures: `BOOT-MAP.md` section 2 states a 95,248-byte PICO
flash image and the rump note states 95,756 on 2026-11-09 (C). Neither
reproduces on current main because the tree moved. Block size is 1 KiB
(`DEV_BSIZE`), `NBUF` is 4 (`machparam.h:56`), so the buffer cache is 4 KiB.
The SD driver has no static sector buffer (`sd.c`, `sdspi.c`).

Text estimates (E, anchored on M and C figures): a stripped F2FS-like store
(two logs, no threaded logging, no inline data, no extended attributes) at
12 to 20 KB Thumb-1 text, anchored on the measured LittleFS 15,330 B and the
C-cited NetBSD v7fs core 7,947 B and msdosfs 26,149 B (compile-only,
`tiny-rump-components.md:36-38`). Against 8.7 to 11.4 KB of PICOCALC headroom
the estimate does not fit without removing other features. In SRAM, the
real-geometry floor of about 32 KiB exceeds the free SRAM of every kernel
except PICO_UART, which carries no SD driver,
and the hypothetical 1 KiB-block floor of 8 to 13 KiB leaves at most 3 KB on
the SD kernels.

## 3. Layering

### On NOR: log on log

The root runs on Dhara: 1 KiB pages, 8 KiB blocks, 8-page checkpoint groups
(7 user pages plus one metadata page), 989 usable sectors of 1,536 raw, gc
ratio 4 (C, `sys/arch/rp2040/doc/dhara-geometry.txt`, `dev/flash.h`). Dhara's
own cost is map.o 1,558 B and journal.o 2,418 B of text (M, objects in the PICO
build before linker garbage collection) and flash.o 2,728 B text, 526 B bss
(M). Main issues a Dhara checkpoint on every root write request
(`dev/flash.c:965`, `fl_sync()` at :661, M by reading); the unmerged change
that moves it to sync is in section 5.

A log above Dhara does not help for three reasons.

- Cleaning multiplies. A file-system cleaner copies live blocks, and Dhara
  sees every copy as a new write that it later cleans in turn, so the two
  write amplifications multiply. With the FIFO closed form under uniform random
  writes (recomputed here: 2.20, 2.69, 3.52, 5.18 at live share u = 0.75, 0.80,
  0.85, 0.90), u = 0.8 at both layers gives about 2.69 x 2.69 = 7.2 (D, an
  assumption of uniform writes at both layers, unmeasured).
- Dhara needs trim to see the upper layer's free space. Dhara counts every
  sector written and not trimmed as live. Without trims, a log that rewrites
  into fresh logical sectors drives Dhara's live share toward 989 of 989,
  where the host model gives 6.28 page writes per write against 1.20 at 758
  live sectors (C, `flash-storage-expectation-plan.md` and the ordering
  draft, host model, not board). `dhara_map_trim` exists (`dhara/map.c:463`)
  and no kernel path calls it (M, grep over `sys/kern`, `sys/sys` and
  `sys/arch/rp2040/dev`).
- Reserve stacks. Dhara already holds 35.6 percent of the raw root back
  (989 of 1,536 sectors, D). F2FS-like metadata of 11 to 16 percent plus a
  cleaning reserve on 989 sectors leaves at most about two thirds of the 989 for
  files (E).

What an LFS directly on raw NOR would replace: Dhara's map, journal, checkpoint
group and gc (about 4 KB of text, M) and the 8/7 checkpoint padding. It
would have to supply its own wear leveling and write-order guarantees with
4 KiB erase sectors and 256 B program pages (C, `flash-traffic-per-workload.md`).
The saving is about 4 KB of text and the padding; the cost is the only
storage stack with board evidence on this port (E). Not recommended.

### On SD over SPI

What the driver does today (M, grep and read, `dev/sd.c`, `dev/sdspi.c`):
commands CMD0, CMD8, CMD55, ACMD41, CMD58, CMD16 (non-SDHC), CMD9, CMD17 and
CMD24. A grep for `CMD(10|12|13|18|25|32|33|38)` and `ACMD(13|22|23)` and for
`erase|trim|discard` over the four files finds no command and only prose.
Transfers are single 512-byte sectors, polled at 12.5 MHz, one 1 KiB buffer
being two commands (`sd.c:376-382`). Write busy waits up to 500 ms per command.
No retry, no cache.

What the specification says (C, SD Physical Layer Simplified Specification
v6.00 mirrored copy (sha256 in Sources read), fetched 2026-11-15; confirmed by grep of its text here):

- CMD32 and CMD33 set the erase range; CMD38 starts the erase. All three are
  valid in SPI mode (Table 7-3, 7.3.1.3). In SD mode CMD38's argument selects
  Erase, Discard (value 00000001h) or FULE (00000002h); Section 4.3.5.2 states
  that Discard "is supported for SDHC and SDXC cards only and not supported
  through SPI interface", and the SPI table row for CMD38 repeats that FULE
  and Discard are not supported. Discard and FULE first appear in the Simplified series at v6.00 (its
  revision history; section 4.19.1 of the same text says the full
  specification defined them in version 5.10); the v4.10 text read has neither (C).
- Section 7.2.15: in SPI mode "the host shall treat the card as Class 0 no
  matter what Class is indicated in SD Status." The speed-class write
  guarantees (sequential write in recording-unit multiples through free
  allocation units, 4.13) therefore do not bind a card in SPI mode (C).
- Allocation unit (AU): 16 KB to 64 MB in the SD Status register, capped by
  capacity (4 MB up to 32 GB, 64 MB above) (Tables 4-47, 4-48, C). ACMD13
  reading it is marked valid in SPI, with unverified framing.
- CMD25 multi-block write in SPI uses start token FCh and the Stop Tran token
  FDh; ACMD23 pre-erase is valid; CMD23 and CMD20 are not (7.2.4, 7.3.3,
  Table 7-4, C).

What cards reward, from outside the specification (C, tagged by class):
- Article: Bergmann, "Optimizing Linux with cheap flash drives" (LWN,
  2011-02-18): segments of 4 MB typical, aligned 4 MB writes fast, 64 KB
  smallest efficient block, good cards keep five or more segments open and some
  keep one or two, a FAT area of 4 to 8 MB on the test card is handled
  differently from the rest. Measurements of specific 2011 cards.
- Paper: Kim, Agrawal, Ungureanu, "Revisiting Storage for Smartphones", ACM
  TOS 2012: across eight SD cards most random writes ran at or below 0.02 MB/s
  and class labels did not predict application performance; the log-block
  FTL explanation is the authors' hypothesis, and a log-structured file system
  helped SQLite.
- Folklore: the number of open AUs and the FAT-region size of any given card.
  Measure per card; the specification states neither.

The pattern a card rewards is sequential append into at most a few open AUs,
aligned to the AU. A host log reaches that pattern by appending 1 KiB blocks
into one open AU; it does not need to buffer an AU. That separates the
benefit (sequential append) from the cost (a segment buffer, which a
1 MiB LFS default of 4.4BSD cannot supply in 10 KB of SRAM).

## 4. Closer designs

| Design | Segment or block unit | RAM floor | Crash model | Discard story | License |
| --- | --- | --- | --- | --- | --- |
| 4.4BSD-Lite2 LFS (`sys/ufs/lfs`; `lfs_segment.c` is 1,166 lines) | default segment 1 MiB, minimum 64 KiB, block 1 KiB to 4 KiB (`sbin/newlfs/config.h:121-136`) | one segment of block buffers, each block malloc'd separately (`lfs_segment.c:1036-1048`); one 64 KiB minimum segment of blocks is about 64 KiB (E, not read from the allocation path beyond :835) | superblock copies (up to ten, `lfs.h:141`) plus checkpoint; no roll-forward file in the directory | none found by grep | UC Regents notice with four clauses in `lfs_segment.c` (advertising clause 3); read for that file only |
| NetBSD LFS (`sys/ufs/lfs`, 31,802 lines) | same family, adds `lfs_rfw.c` | as above, larger | roll-forward file present (`lfs_rfw.c`) | `fdiscard` vnode op stubbed unsupported (`lfs_vnops.c:199`) | NetBSD Foundation two-clause block plus a UC Regents three-clause block in `lfs_segment.c` |
| LittleFS v2.11.2 | erasable block (any size), cache_size multiple of read and program size | two caches plus lookahead plus one cache per open file; `lfs_t` 128 B, `lfs_file_t` 84 B, `lfs_dir_t` 52 B (M, ARM `nm -S`); with 512 B caches about 1.7 KiB for one file (2 x 512 + 512 + 128 + 84 = 1,748 B, D) | metadata pairs, copy-on-write, commit by checksum; falls back to last good state (DESIGN.md) | none; freeing is "drop it on the floor" and the allocator scans for free blocks (DESIGN.md) | BSD 3-clause, Arm Limited 2017 and the littlefs authors 2022 |
| NILFS2 | log of partial segments, user-space cleaner `nilfs_cleanerd` | cleaner in user space; a kernel segment constructor (no number read) | checkpoint per few seconds or per synchronous write; recovery uses segment boundary flags | `discard` mount option, default off | GPL-2.0+ (SPDX line of `fs/nilfs2/segment.c`) |
| F2FS (section 1) | 2 MiB segment, 4 KiB block | section 2 | checkpoint pair plus roll-forward of fsync | discard after checkpoint, prefree set | GPL-2.0 (kernel); f2fs-tools GPL-2 with LGPL-2.1 on listed files |

LittleFS text is M: `arm-none-eabi-gcc` 16.2.0, `-Os -mcpu=cortex-m0plus
-mthumb -ffunction-sections -ffreestanding -DLFS_NO_MALLOC -DLFS_NO_DEBUG
-DLFS_NO_WARN -DLFS_NO_ERROR -DLFS_NO_ASSERT`, `lfs.c` 15,330 B, `lfs_util.c`
116 B, 5,520 B with `-DLFS_READONLY`. It excludes libc, the block-device glue
and any kernel adapter. This kernel has no file-system switch (section 6), so
an adapter is new plumbing, not a table row. LittleFS also fits as a user-space
library over the raw SD device, which costs no kernel text and no kernel
SRAM (E), at the price of a separate namespace.

License boundary. The kernel and f2fs-tools licenses were fetched from their
primary files: kernel `COPYING` is GPL-2.0 with the Linux syscall note, the sampled
`fs/f2fs` files and the header carry GPL-2.0 identifiers, and f2fs-tools
`COPYING` states GPL-2 with LGPL-2.1 for seven named files (file headers
say "GPL or LGPL version 2" and one unlisted dual-licensed
file). No patent grant was found in those texts. This tree is BSD and ISC, so
no GPL text, record layout or comment enters the port. The entry sizes
tagged "C, source" in section 1 were read from the GPL kernel and f2fs-tools
sources as plain byte counts; no code, layout or comment text was copied. A BSD-licensed donor is expected to be
compatible; 4.4BSD-Lite2 `lfs_segment.c` carries the advertising clause
(clause 3), so the compatibility is a decision for the owner, and NetBSD's `lfs_segment.c` carries a UC Regents three-clause block
beside the Foundation block.

## 5. What the port already established and whether it carries to SD

Status of the code. At 270422ca none of these changes is on main. At
origin/main a849d47a (`git merge-base --is-ancestor`, M) the sparse-image pair
b7c30c9b and b395ff51, with its gate 12eb3102 and the partition bound 1913625f, has merged through PR #548; the others have not:
7da13433 (checkpoint on sync, documentation), fd5c5e6b (the change), 8aa41175
(inode before free with trim) and b28daef4 (reset-cut test) sit on local
branches. 84aa9b3a, 40c24b3a and b7ab3899 carry the subjects of the merged
sparse-image commits under earlier hashes and are not in origin/main; 6b839f10
(force the root checkpoint at shutdown) is also not in origin/main. The ordering note and the three
trim surveys are untracked drafts (sha256 in Sources read). Main still
checkpoints per request, still frees before the inode write, and never calls
trim: M by reading `dev/flash.c:965` and `kern/ufs_inode.c` `itrunc()`, whose
comment promises that pointers reach disk before frees while the only
`iupdat` comes after them.

| Result | Evidence as labeled by its source | Carries to SD? |
| --- | --- | --- |
| Checkpoint on sync, not per request | Board counters, one kernel, single runs: per-request checkpoint 8.00 units per write, sync policy 5.65 to 6.76 (C; a unit is 1 KiB, four program pages, converted here from the logged counters). The sync-policy log has no commit identity and commit 7da13433's text says board evidence is not yet run | Mechanism does not carry: it removes Dhara's padding of an 8-page group. The principle does: batch durability points. An SD write has no checkpoint but waits for programming per command |
| Free blocks out of the image's map | Board: first compile after flashing 1.20 units against model 6.28 at 989 live; 758 of 989 mapped. The 1.20 appears as both a board and a model number | Carries as a tool rule: an SD image writer skips unused AUs; erasing them first needs the SD-mode host or a CMD38 pass (E) |
| Inode before free, trim at free | Host-model and crash-cut test; no board run. The expectation plan on the trim branch (`docs/research/flash-storage-expectation-plan.md`) records host-model cost: create-and-delete with 732 of 989 sectors live costs 10.38 pages per file block with trim against 5.99 without | Carries as a precondition. Erase in SPI mode (CMD32/33/38) returns zeros or ones afterward by the SCR bit; a restored inode pointing at an erased block loses data under the 2.11BSD order, the same hazard as on NOR (D). Erase-per-free is costly: the specification adds 250 ms per partially erased AU to the erase timeout (4.14.1, 4.14.4), so frees must batch at AU granularity (C, D) |
| Cleaning-cost knee near live share 0.8 | The knee is Dhara's cleaner cap: 5 steps, so u at most 0.8 keeps pace (D). The closed form is smooth: 2.69 at 0.80. The phrase "within 11 percent of FIFO from 0.85 up" in two drafts compares against the checkpoint column; against the closed form it is 26 percent high at 0.85 and 21 at 0.90 (recomputed here from the closed form) | Does not carry: the card's FTL is a black box with its own policy. The model carries only to a host-run cleaner, unmeasured on SD |

Flash traffic (C, `flash-traffic-per-workload.md`, board counters, one run per
row): `cc -o d d.c` costs 175 root writes, 5,600 program pages and 350 erase
sectors with the driver and `expr`, `basename`, `sed`; each root write costs
32 program pages and two erase sectors in every root row (D from the table).
Even wear is assumed in the doc's endurance arithmetic, not observed.

## 6. The file-system switch and the cheap parts on UFS

One type. `MOUNT_UFS` is the only entry (`sys/sys/mount.h:38-44`, `MOUNT_MAXTYPE`
1). `smount()` calls `mountfs()` (`sys/kern/ufs_mount.c:92`, :206), which reads
a UFS superblock; there is no operations vector. The only format switch is
`MNT_V7` under `#ifdef V7FS` (:246) and no shipped configuration defines
`V7FS`. PICO and PICO_UART set `NMOUNT=1`; PICOCALC and PICOCALC_PSRAM set 2
(Config files). msdosfs and the NetBSD v7fs core are imported under
`tools/netbsd-imports.manifest` (sha256 of each file) and used by host tools
and tests, not by any kernel (`files.rp2040`; grep, M by reading).

A third type touches `mount.h`, `mountfs()`, `ufs_syscalls2.c:26` (statfs
type), `fsgeom.h`, and each of `balloc`, `free`, `ialloc`, `ifree`, `iget`,
`iupdat`, `itrunc` and `bmap` as `#ifdef` branches (C, with line references from
reading these files). The text cost is not recorded in the tree for `ufs_v7.c`; the
host figures for the imported cores are in section 2.

The free list is the 2.11BSD stack in the superblock (`fs_free[NICFREE]`,
NICFREE 200, `sys/sys/fs.h:26`); `free()` (`ufs_alloc.c:290`) pushes the
block at once and the next `balloc()` can reuse it. Cheap F2FS parts on UFS
without a new on-disk format:

- Prefree set applied at sync. Hook: divert `free()` into a bounded per-mount
  list and release it after the inode's `iupdat`, inside `ufs_sync()`
  (`ufs_syscalls2.c:166`), ahead of `bflush`: on an `MNT_ASYNC` mount
  `free()` writes a chain node with a delayed write when the in-core list
  is full (`ufs_alloc.c`), so a release after `bflush` could let the
  superblock name a chain block that has not been written. A full
  bitmap for a 1 GB SD partition of 1 KiB blocks is 128 KiB (D) and does not
  fit; a bounded array of 64 entries is about 256 B (E) with a forced early
  release when full. Needs the inode-first order in `itrunc()` first.
- Hot/cold separation. UFS already separates inode blocks from data in a fixed
  region (`fs_isize`; `ufs_subr.c` `badblock`, `ialloc` scans [SUPERB+1,
  fs_isize)). On NOR Dhara remaps every logical sector, so the separation is
  invisible. On SD the fixed inode region is a small hot area written in place,
  where a card's FAT-optimized region may help (folklore, unmeasured). An AU-aligned
  partition start is the only change required (E).
- Roll-forward for fsync. UFS has no log. `fsync` here flushes the whole volume
  (`ufs_syscalls2.c:266-288` through `syncip` and `ufs_sync`). Roll-forward needs a new log region and mount-time replay: a new
  format, not a cheap part. Not recommended.

## 7. Recommendation

Ranked, at most five. Each estimate is E unless marked.

1. Land the inode-before-free order (8aa41175 and its reset-cut test
   b28daef4) with the trim call decoupled from the order. Changes: `itrunc()`
   writes the inode before `free()`. RAM 0; text a few dozen bytes. Falsifier:
   the reset-cut test over a discarding backend fails with the old order and
   passes with the new one (the mutant is the calibration). Measure first:
   whether `MNT_ASYNC` mounts keep the forced inode write; the drafts and the
   commit disagree.
2. Characterize one SD card in SPI mode on the board, with the board opt-in
   the repository requires and no driver change beyond a test image. Measure
   single-block write latency, sequential against random across 4 MiB
   regions, open-AU count, and CMD32/33/38 erase time by AU-aligned range
   against a partial AU, and the SCR data-after-erase bit. RAM 0 and text 0 in
   the kernel. Falsifier: sequential append across an AU shows no advantage over
   random 1 KiB writes, which retires the log-structured motive for SD.
3. Align the SD UFS partition start and the inode region to the AU (a
   partition-table and image-tool change). RAM 0, kernel text 0. Falsifier:
   the measured random-write rate of step 2 is unchanged by alignment.
4. CMD25 multi-block write with the Stop Tran token for adjacent sectors of
   one buffer pair. RAM 0; text 200 to 400 B. Falsifier: step 2 shows no
   latency or throughput gain over two CMD24; through the buffer cache the longest run
   is the two sectors of one 1 KiB buffer, so the gain there is expected to
   be small; raw-device I/O passes the caller's whole request to
   `sdstrategy()` (`physio()`, `sys/kern/vm_swp.c`), so runs there are as
   long as the request. Skip if step 2 says so.
5. Bounded prefree list with AU-batched CMD38 erase at sync, on SD only, built
   after steps 1 and 2. RAM 256 to 512 B; text 1 to 2 KB. Falsifier: with the
   erase on and off, step 2's workload shows no improvement in worst-case
   write latency, or a card rejects CMD38 with argument zero in SPI mode. The
   NOR half (trim at free into Dhara) is refuted for compile workloads: the
   host model shows trim raising page writes from 5.99 to 10.38 per file
   block, and on the measured Pico eight compiles cost 1,592 root sector
   erases with trim at free against 1,374 without.

Do not build:
- An F2FS port or clone: GPL source, 4 KiB block minimum (about 32 KiB
  resident, D), 11 to 16 percent metadata at the hypothetical 989-block
  geometry, text beyond headroom.
- A log above Dhara on NOR: multiplies cleaning and needs trims that nothing
  issues.
- A log-structured store replacing Dhara on raw NOR: removes about 4 KB of text
  and gives up the only storage stack with board evidence.
- Six logs, threaded logging and roll-forward: no benefit at one writer and 989
  sectors; roll-forward needs a new format.
- A 4.4BSD LFS port: a user-space cleaner daemon, no file-system switch to
  attach it to, and a four-clause source notice; its 64 KiB minimum segment is
  also larger than the free SRAM (E).
- A LittleFS kernel mount before step 2: 15,330 B exceeds PICOCALC_PSRAM
  headroom of 8,712 B (M).

Measured in this tree: section 2 budget table, LittleFS text and struct sizes,
Dhara object sizes. Read from this tree's source: SD driver command set,
`itrunc()` order. Cited: the F2FS,
SD, LittleFS, NILFS2 and BSD LFS facts, the prior-result numbers. Inferred:
every E tag.

## Open questions for the owner

- Is a four-clause 4.4BSD-Lite2 file acceptable as a named donor?
- May a board run characterize an SD card (step 2)?
- The sdcard.org PDF is behind a terms click-through. Reading a copy needs a
  decision on accepting those terms.

## Sources read

Fetch date 2026-11-15 UTC unless noted; local files at 270422ca, sha256 full.

External (mirrors or primary, as stated):
- FAST 2015, Lee, Sim, Hwang, Cho, "F2FS: A New File System for Flash
  Storage", usenix.org conference page and PDF; `Documentation/filesystems/f2fs.rst`
  and kernel COPYING at kernel commit 23c62304dde2; f2fs-tools commit 83fd39a184df.
- SD Physical Layer Simplified Specification v6.00 (April 2017, copy marked
  Draft 1.01), mirror copy at academy.cba.mit.edu/classes/networking_communications/SD/SD.pdf,
  sha256 86502198b4db01f0f7f7aea459210db969724800733411b5014b5548bb916fd2;
  v4.10 at ercankoclar.com (wp-content/uploads/2017/11, "SD-Specifications-Part-1-Physical-Layer-Simplified-Specification-.pdf");
  v3.01 at cs.utexas.edu/~simon/378/resources/Part_1_Physical_Layer_Simplified_Specification_Ver_3.01_Final_100518.pdf. v7 to
  v9.10 text not read; the official page lists v9.10 as current.
- Bergmann, LWN 428584; flashbench README (android.googlesource.com); Kim et al.
  TOS 8(4), 2012.
- littlefs v2.11.2 `lfs.c` sha256 41b7aa8d6e1601ef26129399cc2e5988e736c82a4b89e31183be787722aebf07,
  `DESIGN.md` sha256 2a8b3459d74b9fa8bdd947738522c46331739405b1fc3d70f057f636a1dea768
  (each quoted phrase grep-confirmed), `LICENSE.md`; kernel NILFS2 documentation
  and the SPDX line of `fs/nilfs2/segment.c`.

Local, project-relative, sha256:
- `sys/arch/rp2040/doc/STORAGE.md` f7eea653e60272036a8e86be965f667d1ba2ac66e5fbd1cf27a491261a279af2
- `sys/arch/rp2040/doc/CAPACITY.md` c79ddd2b99209ea3209c829f5250760729ea3836a655818cbfded56ba3f18006
- `sys/arch/rp2040/doc/BOOT-MAP.md` b11a123888951138bc2eb7f54b62bd3a1f113b567e8871b6ab69fb08fe5ae7b3
- `sys/arch/rp2040/doc/research/flash-traffic-per-workload.md` c4a98f4c02a0dca8836cb26ae4b9b848d8dabed76aefdc3ebbf325e01f91de82
- `sys/arch/rp2040/include/machparam.h` ea1dd5dff87c14bc194472a3c43532b8b362b08dfeee390a351a1807b889ddad
- `sys/sys/fs.h` f83cec2a787728771e7b44af1067d8c06b99179f5b9758f38109207b54005a27
- `sys/arch/rp2040/dev/sd.c` 1e6dfbdf764fd86fc4226c9ade4cb6970282f9a202749345e824dcc1c5fff9dd
- `sys/arch/rp2040/dev/sdspi.c` cb5b3fb4537228e5bf8ad04003d367c5711f14bd6fc812671222b8ff13844c18
- `sys/arch/rp2040/dev/flash.c` a4e3951d19db66a4061a03cd8a9697810e42e2bc9826d7c4e65fe1aff0471f50
- `sys/arch/rp2040/dhara/map.c` 945634b49e429ef26b1099c24d0e40b4de50fcc4bf90546803b4bb9fb2f1f514
- `sys/kern/ufs_alloc.c` 6a003aabf9e53ae3f6e83a32df532f49d6d7fe2b757c24a5b50327e0f4d07a24
- `sys/kern/ufs_inode.c` a1c2abdfe0a286cfee6d81778163d9a8c77b9dc2c191098f898532afeabd77cd
- `sys/kern/ufs_syscalls2.c` bd04a7de3f96ef7ca7a48b2a0ea1334eced369e78131283957b4be4f3c091026
- `sys/kern/ufs_mount.c` 16cf6c52b939a657500db853bbfc4fbfc30af17163a049efd80483d5fbe269f8
- `sys/sys/mount.h` 65a36b93813bcd7a9a2304d8bb8d0bac54c698ecd1eb2f334dc7bbef70fa456f
- `docs/research/tiny-rump-components.md` d03c829eb78e090b11d1f8fd3a8f87bc607cd674452214e0a9a018d2288b9347
- `tools/netbsd-imports.manifest` 91256b57011c6d97d37d7f966bc9895a20e811bb83ea4a6351523f007ae0d2c6
- `docs/research/flash-storage-expectation-plan.md` 90534bc6e656538c86da12b007496a6cdd62141cce93904bdecb1dbbcee69153
- `docs/research/renode-sd-card-model-probe.md` 23bc542f71784023df14df2bbf0a24c240ca92081245ebfe794323497f344412

Untracked drafts, not in the tree (read in another checkout, read-only):
- `docs/research/rp2040-flash-trim-ordering.md` cdecdcfd2ac5e9ffc212c24e7d890672b8ebeff2b73ed5660abc783805beca99
- `docs/research/trim-survey/research-rtos-trim.md` 56ab34b94c5f4fef0c30a25a4955a236821e6a4d4ddf0168a9f47a025f45317a
- `docs/research/trim-survey/research-local-math.md` e66f8a7e11f871f3f56f3f28583ede53052937179b18ccd439e8bbc29ca7a0ea
- `docs/research/trim-survey/research-bsd-trim.md` 27177183f550c3b23abeb3ba16ff7ecda3a332118698ced3ff68c90d78e47f66

BSD_Workspace donors (project-relative to each tree):
- 4.4BSD-Lite2 `sys/ufs/lfs/lfs_segment.c` 1617bef7d0f74fee496e8d5ab17cbf81ede93578dbdf06b602a6b376cec1512e
- 4.4BSD-Lite2 `sbin/newlfs/config.h` 6b6fb06a4616cde67a7cfff676193cdca6425cd63bd6a0ca34614a568987b9b2
- 4.4BSD-Lite2 `sys/ufs/lfs/lfs.h` e9c66ab78565092ed7971444ad90c9da3dad24978804d7b39c4845727ce62768
- netbsd-src `sys/ufs/lfs/lfs_vnops.c` 6fdc2b0ac112b6bb1520a4fd39833f767050330eeb7911ead375192fe33b3223
- netbsd-src `sys/ufs/lfs/lfs_segment.c` 36bd2ded64d615e2113167d73fbd15c26894706af77cafb5db4d578d6734f75a

Unverified from a primary source: the sdcard.org PDF itself (mirrors read);
v7 to v9.10 changes to Discard and FULE in SPI; Part 2 File System
Specification AU and format rules; how ACMD13 frames its data block in SPI;
whether any real card honors an SPI erase as expected; kernel mount of an
F2FS volume at the 9-segment floor; the NILFS2 and LFS segment-buffer RAM
numbers; the UC Regents rescission of the advertising clause (not asserted).
