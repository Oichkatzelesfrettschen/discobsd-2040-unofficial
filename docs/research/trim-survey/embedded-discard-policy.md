# Trim and discard ordering on small flash systems: sources and design ranking

Fetched 2026-11-14. Bases: D = github.com/dlbeer/dhara/blob/master (clone 0e083ded); L = github.com/torvalds/linux/blob/master; Z = github.com/zephyrproject-rtos/zephyr/blob/main; R = path in the target tree. `unverified:` = no fetched source.

## 1. Dhara (D/dhara/map.c, D/dhara/journal.c, D/include/dhara/map.h)

- `dhara_map_trim` runs `auto_gc`, then `try_delete`. `try_delete` calls `trace_path` (reads only); an unmapped sector returns success with no write. Otherwise it copies a cousin sector's page to the journal head with a path omitting the deleted sector (`dhara_journal_copy`), then `m->count--`. Cost per mapped trim: one 1 KiB sector copy plus `auto_gc` work. Journal size does not shrink; the live count drops by one. Reading an unmapped sector returns 0xff.
- Durability: map.h says after `dhara_map_sync` "all changes to date are persistent and durable"; README: write and trim are atomic and the state "rolls back to the last synchronization point". `push_meta` (journal.c) programs a checkpoint page when a group fills, so a crash restores an enqueue-order prefix at checkpoint granularity. A trim enqueued after a metadata write is durable only if that write is. The vendored copy (R sys/arch/rp2040/dhara/) differs from upstream by validation hardening in the first 40 diff lines read.
- GC: `dhara_map_gc` examines one tail page; `raw_gc` copies it to the head if `trace_path` still maps its sector there. `auto_gc` acts only when `dhara_journal_size >= dhara_map_capacity`, then runs `gc_ratio + 1` tail steps. `dhara_map_capacity` = journal capacity - `cap/(gc_ratio+1)` - `DHARA_MAX_RETRIES << log2_ppb`.
- Target: R sys/arch/rp2040/dev/flash.h sets `FLASH_GC_RATIO` 4; R sys/arch/rp2040/doc/dhara-geometry.txt gives 8 KiB blocks (192), 1 KiB pages, 8-page groups, capacity 989. Check: 188 x 7 = 1316; 1316 - 263 - 64 = 989.
- The cleaner is page-granular FIFO. Inference from `auto_gc`: the journal pins at 989 pages. A write adds one page and a garbage tail step removes one, so with s steps per write s(1-u)=1 and A = 1/(1-u), u = live fraction of tail pages. s <= 5 gives u <= 0.8 and A <= 5 (5.71 with 8/7). Fully live, every step copies: A = 6 (6.86) and the journal grows into the reserve.

## 2. Linux ordering reference

- ext4 `discard`: `ext4_mb_clear_bb` (L/fs/ext4/mballoc.c) records the freed extent against the running transaction and forbids reuse before commit. `ext4_journal_commit_callback` (L/fs/ext4/super.c) calls `ext4_process_freed_data`, which runs `ext4_free_data_in_buddy`, then queues discards if `test_opt(sb, DISCARD)`. Off by default (docs.kernel.org/admin-guide/ext4.html).
- `fstrim` (`ext4_trim_fs`, `ext4_trim_all_free`, `ext4_trim_extent`): scans the in-memory buddy bitmap, which gains freed blocks only after commit. `ext4_trim_extent` marks the extent used (`mb_mark_used`), drops the group lock, discards, frees.
- F2FS: `f2fs_write_checkpoint` (L/fs/f2fs/checkpoint.c) calls `f2fs_clear_prefree_segments` (L/fs/f2fs/segment.c) only after `do_checkpoint` succeeds; `issue_discard_thread` issues them.
- UBIFS (L/fs/ubifs/gc.c): `ubifs_gc_start_commit` unmaps LEBs with only dirty and free space; index LEBs unmap in `ubifs_gc_end_commit` because dirty index nodes may be needed for recovery. JFFS2 `jffs2_mark_node_obsolete` (L/fs/jffs2/nodemgmt.c) marks in place. MTD `ftl_discardsect` (L/drivers/mtd/ftl.c) updates its map at once, unordered with file system metadata; `mtdblock.c` has no discard.

## 3. RTOS file systems and FTLs

| System | Call and trigger | Crash loss? |
|---|---|---|
| LittleFS (littlefs lfs.c) | No trim. `lfs_alloc_scan` rebuilds a `lookahead_size` bitmap by `lfs_fs_traverse_` over committed metadata; `lfs_fs_gc` (lfs.h) is an optional idle pass that fills it | No: free is derived from durable state |
| SPIFFS (pellepl/spiffs src/spiffs_nucleus.c `spiffs_page_delete`) | Clears header flags in place; `SPIFFS_gc` erases later | No: delete is the metadata |
| Zephyr NVS, ZMS (Z/subsys/kvss/nvs/nvs.c, zms/zms.c) | `nvs_delete` writes a zero-length entry; no trim match in either file | No |
| Zephyr FatFs glue (Z/modules/fatfs/zfs_diskio.c) | `CTRL_SYNC` forwarded; `CTRL_TRIM` hits `default: RES_PARERR` | Trim not forwarded |
| ChaN FatFs `FF_USE_TRIM` (ESP-IDF ff.c `remove_chain`) | `disk_ioctl(CTRL_TRIM, {first, last LBA})` per contiguous run, after `put_fat(clst, 0)` edits the cached FAT window; result ignored (elm-chan.org dioctl) | Yes: trim precedes the FAT sector write (inferred from code order) |
| RT-Thread (dfs_elm.c) | `CTRL_TRIM` to `RT_DEVICE_CTRL_BLK_ERASE` | As FatFs |
| NuttX SmartFS (nuttx fs/smartfs/smartfs_utils.c `smartfs_deleteentry`; drivers/mtd/smart.c `smart_freesector`) | `BIOC_FREESECT` per chain sector, before the directory entry is removed; `smart_freesector` programs the released bit in place | Yes: entry still names freed sectors until removed |
| FileX + LevelX (eclipse-threadx) | `_fx_utility_FAT_entry_write` sends `FX_DRIVER_RELEASE_SECTORS` when a cached FAT entry goes free, before flush, if `fx_media_driver_free_sector_update`; LevelX `_lx_nor_flash_sector_release` clears bits in place | Yes (fault-tolerant mode `unverified:`) |
| Mbed OS `BlockDevice::trim` | Hint: blocks "no longer in use"; default returns 0 | Caller's duty |
| FreeRTOS+FAT (Lab-Project-FreeRTOS-FAT ff_ioman.c/.h) | No trim or discard match | n/a |

NXFFS and Zephyr `flash_area`: not read.

## 4. Batched trim

`fstrim` trims only what committed allocator state calls free. No surveyed small system runs a periodic trim pass; LittleFS `lfs_fs_gc` and SPIFFS `SPIFFS_gc` are the nearest idle passes and do not discard. Safe by construction only when the allocator state read is durable and consistent with the inodes (Section 6 shows UFS is not).

## 5. Write amplification of log cleaning

- Rosenblum and Ousterhout, SOSP 1991 (https://people.eecs.berkeley.edu/~brewer/cs262/LFS.pdf): write cost = (N + N*u + N*(1-u))/(N*(1-u)) = 2/(1-u), reads included; writes only 1/(1-u).
- Desnoyers, "Analytic Modeling of SSD Write Performance", SYSTOR 2012 (https://www.ccs.neu.edu/home/pjd/papers/pjd-systor12.pdf), Eq. 1-4: LRU = FIFO, uniform random, alpha = physical/logical = 1/rho: A = 1/(1 - e^(-alpha/A)) = alpha/(alpha + W(-alpha e^(-alpha))), W the Lambert function. Table 1 matches simulation to four digits. Greedy is approximate.
- Also: Hu et al., SYSTOR 2009 (https://www.systor.org/2009/papers/2_2_2.pdf, slides, windowed greedy); Bux and Iliadis, Perf. Eval. 2010, doi 10.1016/j.peva.2010.07.003; Van Houdt, SIGMETRICS 2013; Stoica and Ailamaki, PVLDB 6(9) 2013, doi 10.14778/2536360.2536372 (hot/cold separation). Abstracts only; no closed form extracted.

Pages programmed per write; Eq. 2 solved numerically, rho = live / 989:

| rho | A (FIFO) | x8/7 | measured |
|---|---|---|---|
| 0.75 | 2.20 | 2.51 | 1.20 |
| 0.80 | 2.69 | 3.08 | 2.34 |
| 0.85 | 3.52 | 4.02 | 4.45 |
| 0.90 | 5.18 | 5.92 | 6.28 |
| 1.00 | diverges; cap 5, or 6 fully live | 5.71 / 6.86 | 6.28-8.00 |

Inference: from 0.85 up the measurements are within 11 percent of FIFO but exceed the `auto_gc` cap at 0.9 and 1.0 (by up to 17 percent); the excess is unexplained (sync padding is a candidate). Below 0.85 they sit under FIFO, plausibly because a first burst has not pinned the journal.

## 6. Target facts that constrain the designs

- R sys/kern/ufs_alloc.c `free()`: the block number goes into the in-core `fs_free[]` array and sets `fs_fmod`; when the array is full (`fs_nfree >= FS_NICFREE`) the freed block itself is overwritten with the chain node and written (`bwrite`, or `bdwrite` with `MNT_ASYNC`). That block is live data and must never be trimmed.
- R sys/kern/ufs_subr.c `sync()`: the superblock is written by `ufs_sync` only when `fs_fmod` is set, and a mount with `fs_flock` or `fs_ilock` held is skipped. The on-disk free list lags in-core state both ways: a block referenced by a written inode can still be listed free, and a freed block can still be referenced by an older durable inode. "The durable free list says free" is insufficient.

## 7. Ranked designs

Shared invariant: block b is trimmed only if no crash-recoverable inode or indirect block references b, and b is not reallocated between that check and the trim (ext4: commit-before-reuse, `mb_mark_used`). Dhara's journal gives prefix durability, so a trim enqueued after the unreferencing writes reached `dhara_map_write` suffices; a buffer-cache entry not yet written does not count.

1. (c) Batched sweep at idle or shutdown. Preconditions: run right after `sync()` plus `dhara_map_sync` with `fs_fmod` clear and `fs_flock` held, trim only listed free blocks, never chain blocks. After a crash, sweep only after fsck. RAM: 0, or a 192 B bitmap bounding work (`try_delete` on an unmapped sector writes nothing). Code: about 150 bytes (guess). Cost: one 1 KiB copy per mapped free block; bound per pass. Failure modes: chain block trimmed; sweep racing allocation; stale inode referencing the block.
2. (b) Queue frees (192 B bitmap or about 2 B per entry), apply after `dhara_map_sync`. Code about 100 bytes. Only sync is a barrier (no checkpoint callback). Failure modes: a queued block reallocated before apply must be dropped; a crash loses the queue, costing only missed trims.
3. (a) Online trim inside `free()`, metadata first. RAM 0; code about 30 bytes. Unsafe as written: `free()` runs during truncate before the inode write, and the superblock lags. A durable trim then precedes a lost inode and reads return 0xff. The fix is a deferred hook, which is (b).
4. (d) Hybrid: (b) after sync, (c) at boot or idle. About 192 B and 250 bytes of code; covers trims lost to crashes. Calibrate first on a known-good (free block) and known-bad (live or chain block, refused) input.
