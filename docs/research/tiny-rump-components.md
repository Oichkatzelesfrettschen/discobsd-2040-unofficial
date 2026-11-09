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

`tiny-rump-components/measure.sh [NETBSD_SRC [OUTDIR]]` regenerates every size
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
  `TIMERAWL` (`0x40054028`, `sys/arch/rp2040/rp2040/machdep.c`), which has
  no latch; a 64-bit reader that tolerates concurrent readers samples
  `TIMERAWH`, `TIMERAWL`, `TIMERAWH` and repeats while the two high reads
  differ.
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
so the Pico cannot address it: the Pico reaches the battery level, both
backlights and power-off through the controller's registers `0x0B`,
`0x05`, `0x0A` and `0x0E` at I2C address `0x1F`, which
`sys/arch/rp2040/doc/PICOCALC.md` maps against the vendor firmware. The quoted object sizes are the `-O0`
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

## The rump structure reduced to its working parts

Six rules carry what a rump kernel does into a system this size; each rests
on a measurement or a source reading above or below.

1. A component is a state machine over a block contract. Its host
   interface is its undefined-symbol list, not a fixed hypercall table:
   the `v7fs` core needs `kern_assert`, `printf` and six string functions;
   a kernel backend needs `bread`, `brelse`, `bwrite`, `getblk`,
   `kmem_zalloc`, `kmem_free` and four `mutex_*` calls; the descriptor
   backend needs `lseek`, `read`, `write` and `fsync`.
2. The hypercall layer is the host backend, one file per host. NetBSD runs
   the same `v7fs` core over vnode I/O (`v7fs_io_kern.c`), over `mmap`
   (`v7fs_io_user.c`), and inside a rump kernel through the first; DiscoBSD
   adds a fourth, `usr.sbin/v7fs/v7fs_io_fd.c`, in 296 bytes of text.
3. The host owns object lifetime. `v7fs_file_deallocate()` removes the name
   and drops the link count; releasing an unlinked inode and its blocks is
   `v7fs_reclaim()` in NetBSD's vnode layer. A host that skips the step
   leaks both on every removal, which `check-v7fs-core` caught in the first
   draft of the tool's `rm`.
4. Memory is a fixed pool sized to the deepest nesting. With
   `STATIC_BUFFER` the core draws block buffers from three 512-byte slots in
   `struct v7fs_self` (2,016 bytes with the superblock cache, 2,032 with
   the `V7FS_EI` byte-order operations), the count the NetBSD kernel uses;
   exhaustion returns NULL, and all 22 callers of
   `scratch_read()` turn NULL into an error. Without `STATIC_BUFFER` the
   core calls `malloc()` per read and never checks the result, so every
   DiscoBSD build defines it.
5. Byte order is a host parameter. `V7FS_EI` selects the order at mount
   time (little, big, or the PDP-11 order of V7's own disks) for 928 more
   bytes of text than a single native order.
6. Selection happens at build time. A Makefile names the sources a program
   or kernel links, so components need no run-time registry; the link sets
   a rump kernel uses to find its components have nothing to collect here.

## The DiscoBSD v7fs program

`usr.sbin/v7fs` links the imported core and newfs library behind the
descriptor backend and a command front end: `newfs`, `ls`, `cat`, `get`,
`put`, `mkdir`, `rm` and `df` on an image file or a raw device, with `-p`
for the PDP-11 order. The DiscoBSD build, from `bmake build`, is 21,978
bytes of text, 360 of data and 2,356 of bss: 8,274 of text are the NetBSD
core in every byte order, 1,900 the newfs library, 2,985 the front end, 296
the backend, and the rest DiscoBSD's libc; the bss is the static
`struct v7fs_self` and the descriptor record, and nothing comes from
`malloc()`. Its largest stack frame in the core is 224 bytes and its
deepest direct call chain 376 bytes on Cortex-M0+, plus the 512-byte block
buffer each data callback holds, so the program occupies about 25 KB of
the 144 KB window. It installs to `/usr/sbin` and no manifest lists it.

The front end guards the host's side of the contract. `newfs` checks the
geometry before touching the target: at least the 128 sectors a mount
accepts, no more than v7fs addresses or `off_t` offsets, and a data area
that holds the 50-entry free-block cache. `put` and `mkdir` compute the
data and index blocks a file needs, plus a parent-directory block, and
refuse with `ENOSPC` before allocating, because `v7fs_datablock_expand()`
keeps the blocks it took when it fails part way. `get` checks that its
source is a regular file and that its destination is neither the image nor
another name or device node for it before truncating, and every modifying
command reports a failed `fsync()`.

The imported files compile in NetBSD's gnu17 dialect with the two warning
exemptions `check-v7fs-core` documents; the DiscoBSD files compile as
strict C17 with `-Wpedantic`, seeing the imported headers through
`-isystem`. Two host gaps sit in shims under `usr.sbin/v7fs`:
`netbsd_compat.h` supplies the BSD `<sys/cdefs.h>` macros and byte-order
constants DiscoBSD's headers lack, from the compiler's predefines, and
`compat/discobsd/inttypes.h` maps `<inttypes.h>`, which DiscoBSD's libc
does not ship, onto `<stdint.h>` for these sources alone. DiscoBSD's
`include/assert.h` expands to a statement block that calls `fprintf()`
and `exit()` undeclared, which ISO C's `assert()` forbids; the imported
files therefore build with `NDEBUG`, and a conforming `assert()` with its
libc support function is a separate libc change, since it relinks every
program.

Packed structures are safe on the target for a reason worth recording.
The core addresses `uint32_t` arrays at offset 2 of packed superblock and
free-list blocks. Both arm-none-eabi-gcc and clang for thumbv6m lower a
direct packed-member store to byte stores and the `memcpy`/`memmove` of
those arrays to library calls, so no unaligned word access reaches the
Cortex-M0+, which faults on one. Forming the offset-2 `uint32_t` pointer is
still undefined in ISO C, and clang's alignment sanitizer on macOS reports
it at `sbin/newfs_v7fs/main.c:177`.

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
- The host, for tests: the same core plus NetBSD's own userland backend
  and newfs library, built with the host compiler. `check-v7fs-core`
  (`tests/v7fs_core`) is this host.

The user-space server and the core-1 driver engine are not placements.

## NetBSD synchronization

`tools/netbsd-imports.manifest` records the netbsd-src revision and the
SHA-256 of each imported file. An imported file keeps its NetBSD path, so
`sys/fs/v7fs/v7fs_inode.c` names the same file in both trees, and a
directory holding an imported file holds nothing else. `tools/netbsd_imports.py`
carries the procedure: `sync NETBSD_SRC [--revision REV] [--add PATH]
[--patch PATH=PATCH]` reads the listed files from the git objects of the
revision, never from the checkout's working tree, applies the recorded
patches, rewrites the hashes and the revision, and prints each path whose
content changed, which is the diff a revision bump puts up for review;
`verify` checks the tree against the manifest without a NetBSD checkout,
so `check-netbsd-imports` runs in CI. Imported files keep their license
headers, and every local change is either a backend or adapter file
outside the hashed set or a recorded patch: a patched file's manifest line
holds its own hash, NetBSD's hash and the patch under
`tools/netbsd-patches`, `verify` reverses the patch to reproduce NetBSD's
bytes, and `sync` stops when a NetBSD change makes the patch stop applying.

Three patches correct defects review found in NetBSD's code, each with a
regression scenario that fails when its patch is reversed:

- `newfs_v7fs-main.patch`: `make_filesystem()` cleared sectors 2 through
  `ilist_size - 1`, treating a block count as an end sector, so formatting
  media that held data left the inode list's last two blocks with stale
  inodes the superblock counted as free.
- `v7fs_inode.patch`: `v7fs_inode_number_sanity()` rejected the last inode
  slot, whose number equals `V7FS_MAX_INODE()`, after
  `v7fs_inode_allocate()` had already decremented the free count;
  `v7fs_inode_inquire_disk_location()` bounded the inode number only under
  `V7FS_INODE_DEBUG`, so a corrupt directory entry could steer an inode
  write into a data block. The patch makes the bound inclusive, restores
  the count when a cache entry names no inode, and bounds every lookup to
  inodes 1 through `V7FS_MAX_INODE()`.
- `v7fs_superblock.patch`: `v7fs_superblock_sanity()` bounded the signed
  free-cache counts only from above, so a negative count, or an empty block
  cache while blocks remain free, reached the cache arrays out of bounds.

Each is a candidate for a NetBSD problem report; none has been sent.
`rtems-libbsd` (`builder.py` in BSD_Workspace) imports FreeBSD the same
way, at much larger scale.

The imported set is the ten core files and eight headers of `sys/fs/v7fs`,
NetBSD's userland backend `v7fs_io_user.c`, and `sbin/newfs_v7fs/main.c`
with `newfs_v7fs.h`, the library `makefs` also builds; the disklabel
front end `newfs_v7fs.c` stays out. `sbin/Makefile` builds an explicit
`SUBDIR` list, so the imported `sbin/newfs_v7fs` builds nothing for the
board. With `-Wall -Wextra` on the host the core compiles warning-free
except for an unused parameter in `scratch_remain()`, used only under
`STATIC_BUFFER`, and one packed-member warning in
`v7fs_freeinode_update()`, which takes a
`uint16_t` pointer into the packed superblock; the pointer is aligned
because the cache sits at an even offset, which `check-v7fs-core` asserts
for the host and for Cortex-M0+, where an unaligned halfword access
faults.

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

1. Host test of the `v7fs` core, landed as `check-v7fs-core`. The core,
   `v7fs_io_user.c` and the newfs library format an image, write three
   files and a directory, and read them back; an independent reader built
   from the Seventh Edition layouts checks entries, bytes, and the
   accounting of every block and inode, in the PDP-11 and the
   little-endian order. A consistent but wrong address-order mutant passes
   the core's own read-back and fails the reader, which shows the reader
   is the oracle that matters. `fsck_v7fs` was not used: it shares the
   core under test.
2. DiscoBSD userland `v7fs` tool, landed as `usr.sbin/v7fs`: cross-built
   by `bmake build` at about 25 KB of the 144 KB window, and driven on the
   host by `check-v7fs-core` through the same core and independent reader.
   Board execution waits for the owner's approval; shipping it adds a
   manifest line and a numbered NOTICE section.
3. Import manifest and script, landed as `tools/netbsd-imports.manifest`,
   `tools/netbsd_imports.py` and `check-netbsd-imports`, whose selftest
   rejects an altered byte, a missing file, an unlisted file and a missing
   revision line before `verify` runs.
4. FAT tool from `msdosfs_fat.c`, `msdosfs_conv.c` and `msdosfs_lookup.c`
   plus a buffer shim modeled on `makefs/ffs/buf.c`. Falsifier: the
   `makefs` forks of `denode`, `vfsops` and `vnops` cost more than the
   reused files save, which makes a non-NetBSD FAT implementation the
   better choice.
5. A kernel-linked component, only after a HEAD kernel measures its flash
   headroom and an inode-to-core adapter has a size.

The NanoRump files stay out of this plan. Their usable idea, a component
descriptor collected by a link set, needs a C-identifier section name, a
`KEEP` rule with boundary symbols in `kern.ldscript` and a boot-time
failure policy before it carries a component. No component needs one:
`config(8)` already selects at build time, so boundary symbols added to the
linker script now would describe a section nothing fills.

## Remaining uncertainty

Not run: any link of a NetBSD component against DiscoBSD's libc or kernel,
any board execution, a HEAD kernel build for current flash headroom, and
any measurement of stack depth. The one execution is the host oracle
`check-v7fs-core`, which exercises the core with NetBSD's userland backend
on the host's C library. The compile-only sizes are within roughly 30
percent of a linked build: garbage collection lowers them, and libkern,
libc and backend code raise them. The other-systems table rests on project
pages and papers, not on builds.
