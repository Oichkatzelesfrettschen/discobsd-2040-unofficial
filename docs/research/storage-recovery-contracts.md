# Storage contracts and recovery evidence

The source baseline is `1e9864463c57f11c3a45db37d393652023843171`.
The analysis separates the imported v7fs image tool, the kernel filesystem,
the buffer cache, the flash strategy, Dhara, and the boot ROM. A successful
operation in one layer establishes only that layer's implemented contract.
The current process window is 192 KiB. Historical 144 KiB resource records
retain their original source revisions.

## Address and representation contracts

| Domain | Unit and concrete carrier | Conversion and bound |
| --- | --- | --- |
| Image bytes | `off_t` in descriptor `lseek`, `size_t done` in `fd_transfer` | Check sector range before multiplying by 512; clamp mount sectors to `V7FS_FD_MAX_SECTORS`. Positive short transfers advance the buffer and descriptor position until 512 bytes complete. |
| V7 blocks | `v7fs_daddr_t` for imported core addresses; `daddr_t` for descriptor callbacks | Blocks contain 512 bytes. Persistent inode addresses carry 24 bits; index/free-list addresses carry 32 bits and still need volume/data-area checks. |
| Kernel blocks | `struct buf.b_blkno`, `b_bcount`, `b_resid`; `DEV_BSIZE` | Kernel buffers contain 1,024 bytes. With `V7FS`, `FSBTODB`, `FSBADDR`, `FSGETBLK` and `FSRESID` select one 512-byte part while preserving its neighboring part. |
| Dhara logical sectors | `dhara_sector_t` passed to `dhara_map_read/write` | Each sector contains 1,024 bytes. A logical sector maps to a separate physical page number through the journal/map. Equal widths do not make those addresses interchangeable. |
| Dhara physical pages | `dhara_page_t` passed to NAND callbacks | `FLASH_FS_OFFSET + page * FLASH_UNIT_BYTES`; `flash_nand_range_valid` checks page, displacement and remaining length. |
| Physical flash bytes | ROM address/count arguments | Program granularity is 256 bytes; erase granularity is 4,096 bytes. One 8,192-byte Dhara erase block spans two physical sectors. |

The test interface uses explicit address domains and checked multiplication.
For a byte offset `offset`, count `length` and bound `limit`, the admitted
range is `0 <= offset <= limit` and `0 <= length <= limit - offset`.
`byte_address` checks the unit multiplication against remaining capacity
before adding an explicit partition/region origin. `split_address` returns
the unit number and displacement. A physical Dhara page requires the root
region origin; a logical sector uses the logical image origin.

The imported core and kernel V7 reader are separate implementations.
`sys/fs/v7fs/v7fs_endian.c` supports the tool's little-endian and PDP-11
orders; `sys/kern/ufs_v7.c` handles little-endian foreign volumes through
the kernel inode algorithms and format descriptor. The ordinary native
filesystem format uses `DEV_BSIZE`; its inode and directory representation
is separate from the imported tool's V7 disk records.

| Persistent V7 field | Width, interpretation and location |
| --- | --- |
| Superblock inode-area end | Unsigned 16-bit block number at byte 0 of block 1; inode blocks start at block 2. |
| Volume and free-block totals | 32-bit values at bytes 2 and 418; their admitted values are bounded by the image and 24-bit address space. |
| Free-block cache | Signed 16-bit count at byte 6, at most 50; 32-bit block entries start at byte 8. Entry zero links a free-list block; zero terminates the chain. |
| Free-inode cache | Signed 16-bit count at byte 208, at most 100; 16-bit inode entries start at byte 210. The total at byte 422 counts mode-zero disk inodes. |
| Superblock flags and time | Four 1-byte lock/modified/read-only fields at bytes 410--413; signed 32-bit time at byte 414. |
| Disk inode | 64 bytes. Mode, signed link count, UID and GID occupy two bytes each; size occupies four bytes at offset 8; thirteen packed 3-byte addresses start at offset 12; the fortieth address byte is unused. Three signed 32-bit timestamps start at offset 52. UID/GID use 16-bit bit patterns; imported memory fields are signed 16-bit. |
| Directory record | 16 bytes: unsigned 16-bit inode number and a 14-byte name. Inode zero marks an unused entry. Names may fill all 14 bytes. |
| Indirect block | 128 four-byte addresses. Zero marks an absent address. Ten direct slots precede single-, double- and triple-indirect inode slots. |

PDP-11 16-bit fields store their low byte first. PDP-11 32-bit fields store
the high 16-bit word first, with low byte first inside each word. PDP-11
packed addresses store high byte, low byte, middle byte. Little-endian packed
addresses store low, middle, high. Byte representation does not establish
persistence at a byte, CPU word, flash program page or filesystem block.

## Writes, ordering and acknowledgement

| Mechanism | Input, volatile state and return | Persistent effect and acknowledgement boundary |
| --- | --- | --- |
| `fd_transfer` in `usr.sbin/v7fs/v7fs_io_fd.c` | One V7 block; descriptor position, byte count and three fixed scratch buffers remain volatile. Boolean success means all 512 bytes transferred; EINTR retries; zero/error returns false. | A failed call can already have transferred a prefix or the whole block. The backend supplies neither transaction rollback nor a flush barrier. |
| CLI image finalization in `usr.sbin/v7fs/v7fs.c` | Cached superblock, command error and descriptor ownership. Preserve the first command error over later cleanup/flush errors. | The CLI checks superblock writeback, `fsync` and close before reporting success. Host `fsync` follows the host's storage contract; board persistence needs the actual kernel/device path. |
| Imported compensation helpers | In-core inode and allocation snapshots, scratch ownership and boolean block callbacks. Return errno values. | Selected failures restore metadata, namespace and allocation ownership when compensation succeeds. Failed compensation returns an error and retains an image-inspection/repair boundary. The format carries no general atomic transaction record. |
| `bwrite`, `bflush`, `biodone` in `sys/kern/ufs_bio.c` | `struct buf` dirty/busy/done/error state and asynchronous completion. `bwrite` returns a synchronous error; `bflush` retains its first error. | Synchronous writes wait for the device strategy. `biodone` latches the first filesystem write error in `mount.m_write_error`; delayed writes have a separate acknowledgement path. |
| `fsync`, `syncip`, `ufs_sync` | Inode state, dirty buffers, superblock modified flag and mount write-error latch. | `fsync` performs inode synchronization and filesystem synchronization. `ufs_sync` writes inodes, dirty data and then the modified superblock; failures restore the superblock dirty flag or propagate the first error. Multiple metadata writes remain separate device requests. |
| `flstrategy` in `sys/arch/rp2040/dev/flash.c` | Kernel block/byte count, partition table, map and shared scratch ownership. Root requests require whole Dhara units. | Root writes call `dhara_map_write` for each sector, then `fl_sync` before `biodone`. Raw swap uses a separate erase/program path; its contents have a different reboot lifetime. |
| `dhara_map_sync` in `dhara/map.h` | Logical-to-physical mapping and checkpoint metadata. Return zero or a negative error. | Its documented successful return promises persistence of changes under the NAND callback contract. Unsynchronized changes carry an unspecified persistence guarantee. The header does not promise that all unacknowledged bytes disappear. |
| `dhara_nand_prog/erase/read/copy` in production `flash.c` | Physical page/block units and checked byte ranges; reads copy from XIP; copy uses 256-byte scratch pieces. | `nand.h` requires checked operation status and ECC/error handling. The NOR adapter copies XIP bytes without ECC. ROM program/erase functions return void; their wrappers return zero after the ROM/XIP sequence. That implementation boundary requires separate integrity and interruption evidence. |
| ROM program/erase wrappers | Flash byte offsets/counts, interrupt masking, XIP disable/restore, cache flush and clock catch-up. | Software-request counters increment before ROM calls. A count measures requested equivalents, rather than electrical completion, wear distribution or atomic persistence. |

Source inspection establishes these call paths and return conventions.
The inspection does not establish electrical program/erase timing or every
failure mode of the connected flash part. Replay the symbol search with:

```sh
rg -n 'fd_transfer|image_close|fsync|fl_sync|flstrategy|flash_program|flash_erase' \
  usr.sbin/v7fs sys/kern/ufs_bio.c sys/kern/ufs_syscalls2.c \
  sys/arch/rp2040/dev/flash.c
rg -n 'FSBTODB|FSBADDR|FSGETBLK|FSRESID' sys/sys/fsgeom.h
```

## Existing Dhara interruption experiment

Pull request 435 at `7aed0038bef102bff48447d4fade23868f7b3e48` carries
`tests/rp2040/dhara_amplification/recovery_test.c`, its runner, and
`docs/research/rp2040-optimization/wear-recovery.md`. Read those Git objects
before importing any experiment. The runner uses the production Dhara
journal/map with a host traffic model; its source records 464 complete-operation
cut boundaries across 989 sectors and a half-page prefix counterexample at
cut 26. The counterexample is a mixed logical sector before the first
acknowledgement. Those counts are the branch's retained report until its
runner receives a fresh execution receipt.

The source rebuilds map/page-buffer state on recovery and checks acknowledged
updates, unacknowledged old-or-new updates and unrelated sentinels. Its copy
hook aggregates 256-byte production subprograms. The partial model replaces
half a 1,024-byte program or half an 8,192-byte erase before interruption.
It establishes a bounded model counterexample; its source does not establish
frequency on silicon, filesystem-wide atomicity or arbitrary bit-subset
coverage. Successful map resumption and intact payloads are distinct claims.

The production design remains unsettled at the payload-integrity and recovery
availability boundary. The deciding alternatives are explicit integrity with
a usable prior version, validated physical interruption assumptions, or a
weaker stated contract. The offline comparison can investigate these choices;
production batching, publication and wear-policy changes require admission
against their persistent representation and ordering contracts.

## Shared host failure and image interface

`tests/storage_recovery/storage_trace.py` records each operation's sequence,
layer, kind, address domain, byte offset/count, phase, transferred bytes and
result. An inactive injection fails calibration. A positive short transfer
returns its prefix count. An error may follow zero bytes, a prefix, or the
whole transfer. An interruption freezes exported persistent bytes before
the core can change them during compensation. A second selected error can
act during compensation.

`v7fs_bridge.c` and `v7fs_backend.py` compile the actual imported core,
formatter and descriptor backend with the production fixed pool and
`NDEBUG`. Only the backend's system-call boundary is renamed. The byte device
is a deterministic persistence model; its exported bytes do not inherit host
filesystem or board durability. `v7_image.py` independently traverses every
allocated inode's direct and three indirect levels, free-list ownership,
free-inode counts/cache entries, directory reachability and link counts.
The reserved bad-block inode remains an explicit reachability exception.
The reader bounds individual file size by the input image size and supports
the types exercised by the fixtures; a larger sparse-file claim requires a
separate bounded reader extension.

The ordinary-error matrix constructs full 1-, 10- and 138-block parent
directories using the production allocator and valid directory records with
many hard links to one empty file. Creation grows a data block alone, a data
block plus single index, or a data block plus double/single indexes. The
oracle reopens persistent state and continued allocation follows recovery.
The matrix distinguishes successful compensation from failed compensation
and interrupted execution. The latter two states carry separate contracts.
