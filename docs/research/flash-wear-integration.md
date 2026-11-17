# Flash wear branches: what main keeps and why

## Finding

Four inputs worked on root and swap flash wear: `rp2040/trim-freed-blocks`
(tip `2d154315`), `rp2040/free-list-sweep` (tip `2c35eff2`, which
contains the first), `rp2040/wear-evaluation` (tip `e88079e1`) and an
uncommitted probe diff in the `rp2040/swap-wear-by-sector` worktree (on
`fcd49239`). Every kernel mechanism among them is either on `main` in a
later revision, refuted by a board measurement, or a measurement probe
whose result `main` already records
(`sys/arch/rp2040/doc/research/flash-traffic-per-workload.md`). None lands as kernel code. Main
kept stale text that still described trim at free as planned; that text
is corrected beside this record.

`unimplemented-storage-work.md` remains the one home for the deciding
measurement and reopening condition of each unmerged item. This record
adds what it lacks: the wear-evaluation branch, the swap probe, the
successor of each mechanism on `main`, and the kernel cost each commit
recorded.

## Method

The comparison base is `main` at `d81b1788`. For each file the branches
change, the added lines of `git diff df48d76d rp2040/free-list-sweep`
were looked up, trimmed, in the file at `main`. A file whose added lines
are absent from `main` was then read against its `main` successor, which
may have moved (the collector-trigger note moved to
`sys/arch/rp2040/doc/research/`). The wear-evaluation merge was compared
with its parents, and the probe diff's hunk lines with
`trim-survey/board/swap/spread/swap-who-wear-probe.patch` on `main`.

## Mechanisms

Kernel text figures are those the source commit's message records, for
PICO and PICO_UART; the commits name no compiler version. "Not measured"
means no record gives the figure.

| Mechanism | Source commit | Files | Measured benefit (source record) | Kernel text, RAM | On main | Disposition |
| --- | --- | --- | --- | --- | --- | --- |
| Dhara checkpoint on sync and group fill (`DIOCCHECKPOINT`, `fl_checkpoint()`) | `fd5c5e6b` | `flash.c`, `ufs_syscalls2.c`, `disk.h` | Board: 32 program pages and 2 erase sectors per root write with a checkpoint per request | PICO 100,395 to 100,515 B; PICO_UART 90,045 to 90,165 B; data and bss unchanged | Yes, later revision (`flioctl()` `DIOCCHECKPOINT`, `ufs_sync()`) | Superseded by main |
| Checkpoint policy held to resets over a NOR model | `e30f1f17` | `tests/rp2040/flash_checkpoint`, `tests/kernel/ufs_disk_test.c` | Gate | None | Yes, `check-flash-checkpoint`, `kernel.ufs-disk.ilp32` | Superseded by main |
| Checkpoint policy text, loss window | `7da13433` | `STORAGE.md`, research notes | Record | None | Yes, later wording | Superseded by main |
| Forced root checkpoint at shutdown; read-only opener refused | `6b839f10` | `shutdown_sync.c`, `flash.c` | Closes the window `sync()` leaves on `updlock` | PICO +100 B, PICO_UART +104 B | Yes (`bdev_checkpoint(rootdev)`, `EBADF`, three shutdown mutants) | Superseded by main |
| Inode written before the frees in `itrunc()` | `8aa41175` (`ufs_inode.c` half) | `ufs_inode.c` | None on wear: arms A and B are equal in every cell of `trim-cost-model.md`. A reset between frees and inode write leaves a missing block instead of a duplicate; `fsck -p` repairs both before any write | Not measured apart from the trim | No | Research only; issue #571 holds the decision |
| Trim at free (`free_trim()`, `bdev_trim()`, `DIOCTRIM`, `fl_trim()`) | `8aa41175` | `ufs_alloc.c`, `ufs_syscalls2.c`, `flash.c`, `buf.h`, `disk.h` | Refuted: 8 board compiles erased 1,592 root sectors with trim, 1,374 without (`trim-survey/board/arms/T.txt`, `B.txt`) | PICO 100,615 to 101,111 B (+496); PICO_UART +496; data and bss unchanged | No | Archive; reopens when a discard costs less than a write |
| Truncate trims at every reset cut (`check-trim-order`) | `b28daef4` | `tests/kernel/trim_order_test.c`, `trim_dhara.[ch]`, `trim_order_mutate.py` | Gate for the trim path | None on the target | No | Not carried: it gates a path absent from main |
| Lookup before trim (`dhara_map_find()` in `fl_trim()`) | `00f5c04d` | `flash.c`, `tests/kernel/trim_cost.c` | A trim of an unmapped sector programs nothing | +40 B on each kernel | No | Archive; it has no caller without trim at free |
| Trim cost arms and deferred free-list sweep | `bf1f3981` | `tests/kernel/trim_cost.c`, `Makefile` | Host model: no sweep depth beat leaving freed blocks mapped in W1, W2, W4; W3 breaks even near 2,600 rewrites | None (host only; the sweep was never a kernel change) | Results in `trim-cost-model.md` | Research only; harness stays on the archive |
| Trim cost record | `1e273f42` | `trim-cost-model.md` | Record | None | Yes, later wording | Superseded by main |
| Later Dhara collector trigger | `2c35eff2` | `dhara-gc-trigger.md`, `tests/rp2040/dhara_trigger` | Host model: live/5 cut W2 from 6.08 to 4.00 pages per block | Not on the branch (model only) | Yes: live/4 less one checkpoint group in `map.c` `gc_trigger()`, `check-dhara-gc-trigger`; board 1,128 erase sectors over 8 compiles | Superseded by main |
| Trim-on-free section of `STORAGE.md` | `2d154315` | `STORAGE.md`, expectation plan | Record | None | Replaced by `trim-cost-model.md` | Superseded by main |
| Board image for the trim arm | `e88079e1` | Merge of `rp2040/flash-image-skips-free-blocks` (`b395ff51`, on main) into `2d154315` | Produced the trim arm's measurement | As trim at free | Its second parent is on main | No content of its own: the merge differs from its parents only in the expectation plan's P8a to P8c rows |
| Erase count per swap sector and swap-outs by command | dirty diff on `fcd49239` | `flash.c`, `sysctl.c`, `vm_swap.c` | 95 of 96 swap sectors erased over 24 compiles, hottest 1.16 to 1.22 times the mean (`sys/arch/rp2040/doc/research/flash-traffic-per-workload.md`) | 848 B bss (GCC 16.2.0, computed from the declarations); text not measured | Patch and captures in `trim-survey/board/swap/spread/` | Measurement probe; superseded by its own recorded result |

No input adds a wear counter beside `machdep.storage_stats`: the swap
probe prints its tables from the `machdep.storage_stats` handler for a
capture, and the root and swap erase totals stay the existing
`STORAGE_ROOT_ERASE_SECTORS` and `STORAGE_SWAP_ERASE_SECTORS`.

## Provenance of the trim arm

`T.txt` records a PICO kernel built on 2026-11-15 at 02:09 UTC
(`kern.version` build 3351); `e88079e1` was committed 24 minutes
earlier. No capture names the source revision, so the link between the
wear-evaluation branch and the trim arm's board figure is an inference
from the timestamps and the branch's content, not a recorded fact.
`e88079e1` has no remote ref; the archive branches hold `2d154315` and
`2c35eff2`.

## Tests not carried

| File | Lines at the archive tip | Reason |
| --- | ---: | --- |
| `tests/kernel/trim_order_test.c` | 774 | Drives `DIOCTRIM` and `free_trim()`, absent from main |
| `tests/kernel/trim_dhara.c`, `trim_dhara.h` | 359 | Device model whose ioctl serves `DIOCTRIM` for the test above |
| `tests/kernel/trim_order_mutate.py` | 96 | Its four mutants rewrite `free_trim()` and the inode write in `itrunc()` |
| `tests/kernel/trim_cost.c` | 236 | Links arms A to C from those mutants and the trim kernel |
| `trim` scenario of `flash_checkpoint_test.c` and its two mutants in `check.sh` | about 90 | Assert `fl_trim()` |
| `bdev_trim` and `fl_trim` stubs in `balloc_test.c`, `ialloc_test.c`, `v7_format_test.c`, `ufs_disk_test.c` | 26 | Link stubs for the trim kernel |
| `tests/rp2040/dhara_trigger/run.sh` and the `stock`, `fixed:N` and `prop:D` trigger selection of `trigger_model.c` | 58 and 55 | The measurement main's note cites as archived; main's `check.sh` gates the shipped trigger with its own model |

Each runs at its own tip on `archive/rp2040-free-list-sweep`, as
`unimplemented-storage-work.md` describes. Running the cost arms on
`main` would need the test build to patch copies of `ufs_alloc.c`,
`ufs_inode.c`, `ufs_syscalls2.c`, `buf.h` and `disk.h` with the trim
delta, the way `tests/rp2040/dhara_trigger/check.sh` patches `map.c`.
That measures a refuted mechanism and gates no failure class main has,
so it is not built.

## Open decisions

- Issue #571 (inode before free): the host model measures no wear cost and
  no wear benefit; the issue's estimate of 2 to 5 percent more root
  traffic is not a measurement. It becomes necessary only with a discard
  at free or a medium whose erase is irreversible before `fsck` runs.
- Whether the trim cost arms should run on `main` through a patched test
  build as described above.
