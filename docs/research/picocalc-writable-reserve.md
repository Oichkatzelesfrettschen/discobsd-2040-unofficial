# PicoCalc writable space and inode floor after sustained use

Status: open decision, tracked by issue 764. The proposed rule below
awaits the owner's choice; no number in it is admitted.

## Recorded counts

Every count describes one fresh installation, before sustained use.

| File system | Free blocks | Free inodes | Evidence class and source |
|---|---:|---:|---|
| Conventional flash root | 66 of 988 | 7 | one paired build, `picocalc-kernel-flash-budget.md` |
| SD card image | 16,751 of 32,768 | 2,350 | one paired build, `picocalc-kernel-flash-budget.md` |
| `/tmp`, made by `mkfs -i 4096` at each boot | 6,045 | 1,533 | qemu-user run, `sys/arch/rp2040/doc/PICOCALC.md` |
| `savecore` reserve on the card | 1024 KB, or the `minfree` file | not applicable | host tests, `PICOCALC.md` |

The two-slot reserve of `sys/arch/rp2040/doc/CAPACITY.md` counts free
entries of the in-core inode table (`NINODE`), not free inodes on a file
system. It keeps its own admission rule in that document.

## Proposed rule

- Flash root: zero block growth and zero inode growth across the
  sustained-use workload. Writes land on the card and on `/tmp`, so the
  flash root stays read-mostly and its Dhara wear stays bounded by
  configuration changes. Seven free inodes leave no working margin for
  any other policy. The boot-time `creat()` of the `/tmp` device node
  truncates and rewrites an inode on the flash root (`PICOCALC.md`, read
  from `itrunc()` and `vn_open_at()`, not measured); the rule holds only
  after that write is removed or explicitly admitted.
- Card root: free blocks at least the larger of 1024 KB and twice the
  greatest growth measured; free inodes at least twice the greatest inode
  growth measured. The factor of two is a proposal; the owner's decision
  fixes it.

## Evidence required before a number is admitted

The workload extends `tests/rp2040/capacity-corpus.json` with repeated
cycles of file creation, editing, compilation where installed, and
deletion on the card. Each class below is a separate record.

1. Physical USB: the CDC-ACM console carries the workload's output
   load on the PicoCalc, with `machdep.usb_*` counters recorded.
2. PSRAM: the workload drives swap traffic through the PSRAM tier.
3. Flash writes: Dhara writes, garbage collection and wear counters over
   the repeated cycles, with flash root growth measured.
4. Reset recovery: forced reset and power removal during writes, then a
   clean `fsck`, Dhara journal resume and a matching `sysctl
   kern.version`. Issue 573 owns the reset and power-interruption matrix;
   this decision consumes its result.

Each run records the kernel identity, `df -i`, `sysctl kern.capacity`
and the Dhara counters before the workload, after each cycle and after
recovery. A Renode run establishes the emulated path and sets no number.

## Falsifier

The flash-root rule fails when any workload cycle grows the flash root by
one block or one inode. The card rule fails when the measured growth
leaves less than the proposed floor on a 32,768-block card.
