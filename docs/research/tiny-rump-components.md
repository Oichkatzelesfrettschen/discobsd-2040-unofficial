Compile-only measurements and source readings; no board run backs any figure.

# Tiny rump components for DiscoBSD on the RP2040

This note tests the proposal that the RP2040 kernel stays a small 2.11BSD
kernel while drivers, file systems and network code arrive as NetBSD rump
kernels, selected by the build and kept in sync with NetBSD sources. It
also audits a companion design, NanoRump, that another investigation wrote
into a working tree without committing it. The result: a stock rump kernel
does not fit the chip in any position, NanoRump's hypercall table cannot
host a NetBSD component, and NetBSD's own source already carries the split
that works -- a component core with no kernel dependencies plus one I/O
backend file per host environment, the split `makefs(8)` and `fsck_v7fs(8)`
use to run kernel file-system code in userland.

## Measured inputs

`tiny-rump-components/measure.sh NETBSD_SRC OUTDIR` regenerates every size
in this section. It cross-compiles each NetBSD source set with
`arm-none-eabi-gcc -Os -mcpu=cortex-m0plus -mthumb`, compiles only and never
links, so a figure is the code before link-time garbage collection and
excludes libkern, libc and the `rumpuser` hypercall layer. Two runs at
netbsd-src `1e8e727e461` (the netbsd-11 branch) with arm-none-eabi-gcc
16.2.0 produced identical output. NetBSD 11.1 (`4198f5370c1`) changes no
file under `sys/rump`, `sys/fs/v7fs`, `sys/fs/msdosfs` or `usr.sbin/makefs`
relative to that revision.

| Source set | Files compiled | text | data | bss |
|---|---|---|---|---|
| rump kernel base (`librump/rumpkern`) | 111, 5 failed | 228,971 | 35,979 | 39,826 |
| VFS faction (`librump/rumpvfs`) | 47, 2 failed | 121,147 | 2,313 | 10,916 |
| network faction (`librump/rumpnet`) | 21 | 38,565 | 158 | 1,780 |
| `net` component | 16 | 65,358 | 704 | 1,140 |
| `netinet` component | 27, 2 failed | 82,222 | 1,448 | 7,056 |
| `v7fs` component, whole | 14 | 15,135 | 128 | 252 |
| `msdos` component, whole | 8 | 26,149 | 116 | 620 |
| `v7fs` core (10 files makefs also builds) | 10 | 7,947 | 0 | 0 |

The rump base's five failures are `locks_up.c` (the uniprocessor
alternative to `locks.c`, so a correct build excludes it), three files that
need build-generated headers (`rump_autoconf.c` needs `ioconf.c`, `cons.c`
needs `nullcons.h`, `atomic_cas_generic.c` needs `atomic_op_namespace.h`)
and `lwproc.c`, whose `lwp_find` prototype conflicts under this probe's
header configuration. The `rumpuser`
interface (`sys/rump/include/rump/rumpuser.h`, `RUMPUSER_VERSION 17`)
declares 62 hypercalls a host must implement.

The DiscoBSD side, from `sys/arch/rp2040/conf/RP2040.ld` and the PICO
kernel built 2026-10-24 (258 kernel commits precede HEAD since then, so its
sizes describe that build only): the kernel flash region is 128 KB minus the
256-byte second stage, the kernel image used 107,902 bytes of text and 248
of data, leaving about 22 KB; the user window `USERRAM` is 144 KB at
`0x20000000..0x20023FFF`; kernel data `RAM` is 106 KB at `0x20024000`;
SRAM4 and SRAM5 (`SCRATCH`, 8 KB at `0x20040000`) hold the USB transmit
ring.

## Why a stock rump kernel fits nowhere

In the kernel: the rump base alone is 229 KB of text, against 22 KB of
flash headroom; base, VFS faction and one file system come to about 360 KB.
The 36 KB of data and 40 KB of bss would fit the 106 KB data region only
by displacing the buffer cache and tables the 2.11BSD kernel already
holds, and the base duplicates that kernel: its largest objects are the
pool and vmem allocators, autoconfiguration, sysctl, the module loader,
kqueue, process and LWP management, and entropy.

As a user process: one process image is resident at a time, in the single
144 KB window (`sys/arch/rp2040/doc/MPU.md`), and OMAGIC a.out text runs
from that window. The base's text exceeds the window before any data,
heap or stack. A rump file-system server could not stay resident beside
its client in any case: every request would swap one image out and the
other in.

## What NanoRump claims and what the tree shows

NanoRump consists of `sys/arch/rp2040/include/nanorump.h`,
`rp2040/nanorump.c`, `dev/nanorump_demo.c`, hardware helpers (`sio_ipc`,
`hw_accel`, `dma`, `axp2101_reg.h`) and six research documents, all
untracked in the primary checkout. It proposes an "8-primitive" hypercall
table, link-set component registration, and three placements: linked into
the kernel, a NanoRump engine on core 1, and a user-space server process.

The component contract fails on a real NetBSD component. The compiled
`msdos` component needs 182 external kernel symbols and `v7fs` needs 172;
106 and 98 of them belong to NetBSD's vnode layer (`vop_*_desc`, `genfs_*`,
`vcache_*`, `ubc_*`, `uvm_vnp_*`, `kauth_*`). The NanoRump table holds 13
function pointers (alloc, free, bio, time_us, delay_us, putchar, spl_enter,
spl_exit, sleep, wakeup, reg_read32, reg_write32, panic), so a NetBSD file
system reaches DiscoBSD through it only with a vnode-layer emulation in
between, which is the 121 KB VFS faction again.

Defects in the code, each read from the source named:

- `kern_nanorump_bio()` corrupts the buffer cache. It takes a header from
  `geteblk()`, replaces `b_flags` with `B_BUSY` (dropping the `B_INVAL`
  that `geteblk()` set) and points `b_addr` at the caller's memory. The
  kernel's own borrowed-header idiom, `swap_with_buf()` in
  `sys/kern/vm_swp.c`, sets `B_PHYS | B_INVAL`. Without `B_PHYS`,
  `getnewbuf()` in `sys/kern/ufs_bio.c` never restores `b_addr`, so the
  header's own data slot is lost for the life of the system; without
  `B_INVAL`, the `LINEAR_BUFFER_CACHE` scan in `getblk()` treats
  the header as a valid cached block for device `makedev(dev_id, 0)`, and a
  later `bread()` of that block returns the caller's memory. The block
  number is also stored unconverted where the scan compares
  `fsbtodb(blkno)`.
- The link set never links. GNU ld defines `__start_SEC` and `__stop_SEC`
  only for a section whose name is a C identifier; `.rodata.nanorump_comp`
  is not one, and `kern.ldscript` folds `*(.rodata*)` into `.text` in any
  case. `measure.sh` calibrates this: the same object links when its
  section is `nanorump_comp` and fails with undefined
  `__start_nanorump_comp` when it is `.rodata.nanorump_comp`.
- `kern_nanorump_time_us()` reads `TIMEHR`, then `TIMELR`, then `TIMEHR`.
  Reading `TIMELR` latches `TIMEHR`, and datasheet 4.6.4 calls the latched
  pair unsafe when two cores, or an interrupt and thread code, both read the
  timer -- the exact condition of the core-1 placement. The kernel reads
  `TIMERAWL` (`sys/arch/rp2040/rp2040/machdep.c`), which has no latch.
- No file is listed in `conf/files.rp2040`, no `Config` option selects one,
  and nothing calls `nanorump_init()`. A component that fails its `init`
  is logged and the boot continues with it absent.

The hardware helpers compile without warnings under the PICO kernel flags
but contradict the datasheet and the tree: `hw_accel.c` reads the divider
quotient one instruction after the divisor write (datasheet 2.3.1.5: valid
from the ninth cycle) and puts divider use in the kernel, which
`MPU.md` and `tools/verify_rp2040_divider_ownership.py` (the
`check-divider` gate) exclude; `dma_channel_start()` writes a non-trigger
alias, so it starts nothing, and its read-modify-write clears the
write-one-to-clear error bits; the I2C DREQ numbers are the PWM_WRAP
numbers (I2C0/1 are 32 to 35); `axp2101_reg.h` names the limit and status
registers as rail enables (DCDC enable is `0x80`, LDO enable `0x90`), and
`dev/picocalc.h` records that the STM32 keyboard controller owns the PMIC,
so the Pico cannot address it. The quoted object sizes are the `-O0`
sizes; under the kernel's `-Os` the helpers are about 40 percent smaller.

The research documents place the user window at `0x20010000..0x20033FFF`
(the linker script and `MPU.md` give `0x20000000..0x20023FFF`), name call
chains through functions absent from `sys/` (`sdcmd`, `spi_xfer`,
`usb_rx_handler`, `usb_cdc_read`; USB input is `usb_rx_done` to
`ttyinput`), count SRAM4/SRAM5 as free, and state a stock librump cost of
over 1.2 MB code and 256 KB RAM with no source; the measurement above is
about a quarter of that. Their "PASS (HARDENED)" verdict cites no build,
link or test receipt.

Two NanoRump placements contradict settled analysis. The user-space server
needs a second resident image, which the layout excludes. The core-1 driver
engine contradicts `rp2040-second-core.md`, which keeps drivers and
incrementally changing state on core 0, gives core 1 only re-executable
service work behind single-writer rings, and notes that `dev/usb.c` relies
on core 1 never being launched. The SIO FIFO round trip of 36 cycles
counts push, interrupt entry and pop each way and omits the handler body,
the exception return, the wakeup and the I/O itself.

## The split that works: component core plus host backend

NetBSD already separates file-system logic from its environment at file
granularity. In `sys/fs/v7fs`, ten files (`v7fs_endian`, `superblock`,
`superblock_util`, `inode`, `inode_util`, `datablock`, `dirent`, `io`,
`file`, `file_util`) form a core of 7,947 bytes of text with no data or
bss, whose only external symbols are `kern_assert`, `printf`, six string
functions and one compiler helper. The environment enters through one backend file:
`v7fs_io_kern.c` (232 lines) for the kernel, needing `bread`, `brelse`,
`bwrite`, `getblk`, `kmem_zalloc`, `kmem_free` and four `mutex_*` calls,
and `v7fs_io_user.c` (172 lines) for `makefs` and `fsck_v7fs`. The vnode
adapter (`v7fs_vnops.c`, `v7fs_vfsops.c`, `v7fs_extern.c`) is a third,
separable layer and the only one that needs the vnode symbols.

FAT splits less cleanly. `makefs` compiles `msdosfs_fat.c` (3,385 bytes
text, no vnode symbols), `msdosfs_conv.c` (2,856, none) and
`msdosfs_lookup.c` (3,064, seven vnode symbols) from `sys/fs/msdosfs`
unmodified, and replaces `denode`, `vfsops` and `vnops` with userland
forks that run on the 241-line buffer shim `usr.sbin/makefs/ffs/buf.c`.

This is the rump idea at the size this chip affords: the reused unit is a
component core, the host contract is the backend's symbol list rather than
a fixed hypercall table, and each host writes its own backend instead of
emulating NetBSD's kernel. DiscoBSD's buffer cache already exports the
four buffer calls under their NetBSD names, so a kernel backend reduces to
allocation and locking: `kmem_zalloc` maps to the kernel allocator and
`mutex_*` to `spl` sections on one core.

## Placement and build selection

A component core is placed in one of three hosts, each selected by the
mechanism the tree already has:

- A userland tool, the first target. The tool links the component core,
  a DiscoBSD userland backend modeled on `v7fs_io_user.c` or
  `makefs/ffs/buf.c`, and a small command front end, and runs as one
  ordinary process, which matches the one-resident-image model. A `pack`
  line in `distrib/rp2040/mi.rp2040` puts it in the image.
- The kernel, only within measured flash headroom. A `Config` option such
  as `options V7FS` selects `optional v7fs` entries in
  `conf/files.rp2040`; BSD `config(8)` then adds the core and a DiscoBSD
  kernel backend to the generated Makefile, and an unselected component
  adds nothing. A kernel mount additionally needs an adapter from the
  2.11BSD inode and mount interfaces to the core, which NetBSD does not
  supply and which has no size estimate yet.
- The host, for tests: the same core plus NetBSD's own userland backend,
  built with the host compiler, checked against images made by NetBSD's
  `makefs`.

The user-space server and the core-1 driver engine are not placements.

## NetBSD synchronization

A manifest under `sys/arch/rp2040/` would list each imported NetBSD file,
its NetBSD path, the netbsd-src revision and the file's SHA-256. An import
script copies the listed files from a NetBSD checkout, verifies each hash
against the manifest, and on a revision bump rewrites the hashes and shows
the upstream diff for review. Imported files stay byte-identical to
NetBSD, with their license headers intact, so every local change lives in
a backend or adapter file. `rtems-libbsd` (`builder.py` in BSD_Workspace)
imports FreeBSD the same way, at much larger scale.

## Other systems built on rump kernels

None of these targets a microcontroller; the list comes from one web search
and the BSD_Workspace trees, so it is not exhaustive.

| System | What it does with rump kernels | State |
|---|---|---|
| NetBSD | `rump_server`, the `rump_*` file-system daemons, `ukfs`, `librumphijack` | maintained in-tree |
| Rumprun | unikernel: rump components plus libc on x86 bare metal, Xen and KVM | no commits since May 2020; seL4 and nccgroup forks |
| seL4 Rumprun | Rumprun's bare-metal layer on seL4; Elphinstone et al., 2017, measured little overhead | research |
| Genode | rump file systems and network stack as user-level components | not checked |
| Frankenlibc | rewritten `rumpuser` plus libc; a new platform starts from a memory allocator | not checked |
| LibrettOS | multiserver and library OS from rump drivers | research |
| Barrelfish | rump port in a bachelor's thesis; reports a 13 MB base library | research |
| rtems-libbsd | FreeBSD network, USB and SD code inside RTEMS through a shim | same idea, not rump |
| LKL | the Linux kernel built as a library | same idea, not rump |

## Order of work, with falsifiers

1. Host test of the `v7fs` core. Build the ten core files with
   `v7fs_io_user.c` on the host, read and write an image made by NetBSD's
   `makefs -t v7fs`, and compare with `fsck_v7fs`. Falsifier: the core
   needs a symbol outside the measured list, or the round trip differs.
2. DiscoBSD userland `v7fs` tool. Core plus a DiscoBSD backend over a raw
   device or image file, cross-built, linked, and sized against the 144 KB
   window. Falsifier: linked text plus data, bss and peak stack leaves too
   little window for the tool's buffers.
3. Import manifest and script for the `v7fs` core, with a gate that fails
   on a hash mismatch, calibrated against one altered byte.
4. FAT tool from `msdosfs_fat.c`, `msdosfs_conv.c` and `msdosfs_lookup.c`
   plus a buffer shim modeled on `makefs/ffs/buf.c`. Falsifier: the
   `makefs` forks of `denode`, `vfsops` and `vnops` cost more than the
   reused files save, which makes a non-NetBSD FAT implementation the
   better choice.
5. A kernel-linked component, only after a HEAD kernel measures its flash
   headroom and an inode-to-core adapter has a size.

The NanoRump files stay out of this plan. Their usable idea, a component
descriptor collected by a link set, needs a C-identifier section name, a
`KEEP` rule in `kern.ldscript` and a boot-time failure policy before it
carries a component; `config(8)` already provides static selection without
one.

## Remaining uncertainty

Not run: any link of a NetBSD component against DiscoBSD, any host or board
execution, a HEAD kernel build for current flash headroom, and any
measurement of stack depth. The compile-only sizes are within roughly 30
percent of a linked build: garbage collection lowers them, and libkern,
libc and backend code raise them. The other-systems table rests on project
pages and papers, not on builds.
