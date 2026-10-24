# Cpio rooted creation and publication identity contract

`cpio -i` retains the extraction root, but its current directory walk changes
the process working directory after separately checking each pathname
component. A competing rename can move the selected parent outside the
extraction root before `mkdir(2)` or `open(O_CREAT | O_EXCL)` mutates it. A
later replacement can also make a successful extraction leave an unrelated
inode at the archive pathname.

The repair keeps the POSIX odc format and streaming memory bound unchanged.
Directory creation, directory verification, regular-file staging, completed
file publication, and failed-stage removal all start from the retained root
descriptor. The kernel holds UFS namespace serialization from the first
restricted lookup through each directory mutation. Regular files remain
descriptor-owned until the complete data stream has been written and the
staged inode is published at the requested name.

## Additive rooted interfaces

The public interfaces are:

```c
int openat_beneath(int rootfd, const char *path, int oflag, ...);
int mkdirat_beneath(int rootfd, const char *path, mode_t mode);
```

Both interfaces require a nonempty relative path whose components exclude
dot, dot-dot, symbolic links, and mounted-filesystem transitions. The root
descriptor must identify a directory. Existing `open(2)`, `openat(2)`, and
`mkdir(2)` behavior and syscall ordinals remain unchanged.

`openat_beneath()` applies the existing open flag, permission, descriptor,
device-open, interruption, and cleanup rules after acquiring UFS namespace
serialization. `vn_open_at()` releases the serialization owner after rooted
lookup, optional creation, permission checks, and inode ownership complete,
before the device-open provider or advisory lock can block. Each error path
releases the owner exactly once. `mkdirat_beneath()` keeps the actual parent
inode across `direnter()` and restores that parent's link count after either
directory-body or publication failure while preserving the primary error.

The two calls occupy the next free syscall ordinals. Their generated dispatch,
ARM libc wrappers, a.out libc members, public declarations, Smaller C header
consumer, and board-libc classification change together.

## Cpio ownership and streaming bound

For a directory component, cpio computes the final umask-filtered mode with
owner access, passes that mode to `mkdirat_beneath()`, and accepts `EEXIST`
only after `openat_beneath()` verifies the existing object as a directory.
The utility does not reacquire a pathname to change metadata, so a scheduled
replacement retains its own mode. Cpio no longer changes its current working
directory while extracting.

For a regular file, cpio constructs a same-length staging leaf in the target
parent. Reusing the target leaf width keeps every staging pathname within the
original `CPIO_PATH_LIMIT`. A bounded base-62 search creates the staged inode
through `openat_beneath(O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW)`. Existing
regular entries and symbolic links both occupy a candidate and advance the
bounded search. The shared I/O buffer carries the transient staging pathname
before data transfer and is rebuilt from the retained attempt number for
cleanup or publication, avoiding a new process-window buffer.

The utility writes archive data through the retained staged descriptor. A
truncated archive or write failure calls `unlinkat_beneath()` with that live
descriptor. Successful data transfer calls `publishat_beneath()` with an
absent-target contract. The syscall preflights the target, removes the staged
name, revalidates the target, and performs the final directory-entry commit in
that order. A failure before staged removal preserves the staged name and the
preflighted target state.
A later target-write failure can leave both names absent while the live
descriptor retains the staged inode; the syscall cannot report failure after
target publication. Cpio then conditionally removes a still-named staged inode
and closes the descriptor.

## Deciding controls

The kernel fixtures cover pathname-copy and descriptor precedence, strict
beneath traversal, final and intermediate symbolic links, mounted filesystems,
exclusive regular creation, directory lookup and creation, directory-write
failures, namespace-lock interruption, descriptor and inode cleanup, and
serialization release on every exit and before device-open and advisory-lock
providers. Sixteen warning-clean namei, vnode-open, and syscall mutations each
must fail their named assertion.

The host cpio fixture schedules ancestor relocation immediately before
directory creation, staged creation, and final publication. It schedules a
same-name replacement before publication and during failed-stage cleanup. The
created inode must receive the archive bytes, an unauthorized replacement
must survive, and the utility must report failure whenever the requested final
name cannot be published. A preexisting staging-name symbolic link must
survive while extraction advances to the next candidate. Seven warning-clean
utility mutations cover cleanup identity, direct final-name creation,
current-directory traversal, pathname-based directory chmod, and premature
failure on a staging symbolic link. A separate interposition mutation must be
rejected when its required rooted-open trace is absent.

The existing malformed archive, odc interoperability, strict-C17 sanitizer,
cleanup identity, kernel namei, directory-write, topology, and publication
mutation controls remain regression obligations.

## Resource and evidence boundary

The comparison records both kernels' linked flash and static SRAM, affected
kernel object and frame sizes, cpio text/data/BSS and loaded a.out occupancy,
packed bytes and blocks, selected libc members, packed-root counters, and
simultaneous descriptor and inode ownership. Positive growth requires one
bounded review exception with the remaining 128 KiB kernel and 144 KiB process
window margins.

Matched parent and candidate schema-2 distribution captures use bmake
20260912, arm-none-eabi-gcc 16.2.0, and GNU ld 2.47.20260726. The comparison
reports the following constrained surfaces:

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 107,870 | 108,366 | +496 | 22,706 bytes below 128 KiB |
| PICO `.bss` / SwapRAM / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 95,388 / 14,832 | 95,884 / 14,832 | +496 / 0 | 35,188 flash bytes below 128 KiB |
| `cpio` text / data / BSS bytes | 10,384 / 200 / 884 | 10,760 / 200 / 884 | +376 / 0 / 0 | 11,844 loaded bytes; 135,612 static bytes below 144 KiB |
| `cpio` raw / packed bytes / packed blocks | 10,616 / 8,852 / 10 | 10,992 / 9,144 / 10 | +376 / +292 / 0 | unchanged block count |
| Native board `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged |
| Packed-root data / free blocks | 693 / 286 | 693 / 286 | 0 / 0 | 286 blocks free |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every other shipped a.out and archive selection | measured baseline | unchanged | 0 | unchanged |

The comparer rejects the five positive flash, loaded, raw, and packed-byte
deltas and the changed cpio archive selection under the default nonpositive
growth rule. The candidate requests one bounded exception for the complete
rooted-creation mechanism. Cpio adds `mkdirat_beneath.o`,
`openat_beneath.o`, `publishat_beneath.o`, and `strrchr.o`, and drops
`fchmod.o` and `mkdir.o`. The utility retains its fixed path and I/O buffers
and owns at most
the extraction-root and staged-output descriptors simultaneously. The static
process-window remainder excludes stack and heap admission, which remain open.

Exact recorded production commands replayed with `-fstack-usage` give these
affected-object results:

| Object | Parent text/data/BSS | Candidate text/data/BSS | Delta |
| --- | ---: | ---: | ---: |
| `init_sysent.o` | 804/0/0 | 814/0/0 | +10/0/0 |
| `syscalls.o` | 1,509/0/0 | 1,544/0/0 | +35/0/0 |
| `ufs_namei.o` | 4,988/0/40 | 4,988/0/40 | 0/0/0 |
| `ufs_syscalls.o` | 5,689/0/0 | 6,101/0/0 | +412/0/0 |
| `vfs_vnops.o` | 528/0/0 | 564/0/0 | +36/0/0 |
| `cpio.o` | 3,652/0/776 | 3,989/0/776 | +337/0/0 |

`namei_at()` retains its 392-byte static frame. `publishat_beneath()` grows
from 136 to 240 bytes and becomes its translation unit's maximum; `rename()`
remains 192 bytes. `copen()` grows from 144 to 152 bytes, the shared mkdir
implementation remains 176 bytes, and the new `openat_beneath()` and
`mkdirat_beneath()` wrappers use 24 and 8 bytes. `vn_open_at()` remains 24
bytes.
Cpio's `copy_in()` grows from 128 to 136 bytes and `copy_out()` grows from 96
to 152 bytes; `copy_out()` becomes the translation unit's largest frame.
`main()` remains 32 bytes, while rooted-directory opening uses 24 bytes and
staging-name construction uses 24 bytes. Compiler static frames do not
establish a complete call chain or runtime high-water bound.

The retained receipt hashes are:

| Receipt | SHA-256 |
| --- | --- |
| Parent schema-2 resources | `cd4233a9ef20d0cd5c18a1ceb4041118c325f9d5c8089492765c819f7a749ffc` |
| Candidate schema-2 resources | `7cedb97581ecb6ad23620d5ae71884edad2e396f6723e79d9585d3266e978ee6` |
| Static-resource comparison | `4d7079934d3c0ba7b7a976c1a6de62a19fb672a75619541afdd72f2fa175a810` |
| Exact-command object/frame comparison | `f1e1018384df4a91a25f2d8fb3cf957ccf11ac1ea37779eaf75e8fd829ceb372` |

Host and ILP32 fixtures establish only their scheduled namespace transitions.
Runtime stack and heap high-water, flash write traffic, crash recovery after
each directory write, Renode behavior, and physical-board behavior remain
separate evidence states.
