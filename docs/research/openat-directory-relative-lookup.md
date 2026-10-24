# Directory-relative open contract

The additive `openat(2)` interface starts ordinary pathname resolution at an
open directory while preserving the existing `open(2)` ABI. The mechanism is
a foundation for later archive identity repair. The mechanism does not confine
resolution beneath the selected directory.

## Source and interface boundary

The implementation commits are `2e52ddc4c9590430f3265cf9dde9a3b565a6c140`
and `39ec39ce18c622d013beba5218ada873f93160d5`. The clean parent is
`cae4d6300e23f32167342dbffbb7fd6f63f1c13b`.

`namei_at(struct nameidata *, int)` stages and classifies the complete pathname
before it resolves a directory descriptor. An absolute path selects the
process root and ignores the descriptor. `AT_FDCWD` selects `u.u_cdir`; another
descriptor must name an inode and traversal enforces directory search
permission. `namei()` remains the `AT_FDCWD` wrapper, and `struct nameidata`
retains its layout.

`vn_open_at()` preserves the live `setjmp(&u.u_qsave)` frame through `openi()`.
`O_NOFOLLOW` rejects a final symbolic link, `O_DIRECTORY` requires a directory,
and `O_SEARCH` grants directory search ownership without read or write access.
The shared `copen()` path publishes descriptors for `open()` and `openat()`.
Legacy `open()` preserves its empty-path behavior; `openat()` rejects an empty
path with `ENOENT`.

Syscall 157 adds four argument words after retained syscall 156. Generated
dispatch sources, public numeric headers, ARM and a.out libc archives, the
board-libc classifier, the installed manual alias, and the Smaller C consumer
carry the same interface. Existing syscall numbers, public layouts, and
calling conventions retain their recorded values.

## Deciding controls

The ILP32 fixture links production `ufs_namei.c`, `vfs_vnops.c`, and
`ufs_syscalls.c`. The lookup program passes 64 assertions covering cwd and
descriptor roots, absolute paths, invalid and non-directory descriptors,
search permission, copied-path precedence, traversal references, dot-dot, and
process-root clamping. The open program passes 121 assertions covering flag
combinations, final links, directory and search descriptors, descriptor
publication, allocation failure, interruption, and device-open failure.

Three warning-clean source mutations provide rejection calibration:

- selecting `u.u_cdir` instead of the retained descriptor fails the distinct
  directory identity assertion;
- consulting the descriptor for an absolute path fails the process-root
  assertion;
- following the final symbolic link under `O_NOFOLLOW` fails the `ELOOP`
  assertion before truncation or open publication.

The pathname-origin gate accepts 31 production transitions and rejects its
five existing inventory mutations. Kernel metadata rejects a three-word
`openat` record, pins slot 157 and preprocesses the numeric header through the
ARM assembler. Smaller C repeatedly includes `fcntl.h`, checks the typed
function pointer, and selects the generated `openat.o` archive member.

## Matched target resources

The clean parent and candidate use `arm-none-eabi-gcc` 16.2.0, GNU binutils
2.47.20260726, bmake 20260912, the same generated-input ledger, and separate
immutable command captures. The parent and candidate closure receipts account
for 174 and 166 C/header paths in PICO and PICO_UART, respectively, plus every
installed native and packed-program closure.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO linked flash bytes | 105,990 | 106,382 | +392 |
| PICO_UART linked flash bytes | 93,508 | 93,908 | +400 |
| PICO static SRAM regions | recorded baseline | same extents and allocations | 0 |
| PICO_UART static SRAM regions | recorded baseline | same extents and allocations | 0 |
| Every installed a.out raw/loaded byte count | recorded baseline | same | 0 |
| Every packed a.out byte/block count | recorded baseline | same | 0 |
| Packed-root data blocks and inodes | recorded baseline | same | 0 |
| Native `crt0.o` and `libc.a` bytes | recorded baseline | same | 0 |
| Selected archive/member sets | recorded baseline | same | 0 |

The bounded exception accepts the linked increase because syscall 157 closes
one caller-complete kernel/libc mechanism required by the archive identity
queue. The 128 KiB kernel region retains 24,690 PICO and 37,164 PICO_UART flash
bytes after the change.

Exact production commands replayed with `-fstack-usage` report these affected
objects and static frames:

| Object or function | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| `ufs_namei.o` text/data/BSS | 4,520/0/36 | 4,608/0/36 | +88/0/0 |
| `vfs_vnops.o` text/data/BSS | 400/0/0 | 528/0/0 | +128/0/0 |
| `ufs_syscalls.o` text/data/BSS | 4,489/0/0 | 4,633/0/0 | +144/0/0 |
| legacy `namei()` / `namei_at()` frame | 392 | 392 | 0 |
| new `namei()` wrapper frame | absent | 8 | +8 |
| legacy `vn_open()` / `vn_open_at()` frame | 24 | 24 | 0 |
| new `vn_open()` wrapper frame | absent | 8 | +8 |
| legacy `open()` frame | 128 | 24 plus 144-byte `copen()` | call chain +40 |
| new `openat()` frame | absent | 24 plus 144-byte `copen()` | new call chain 168 |

Compiler frames bound individual functions. Runtime stack and heap high-water,
descriptor-table pressure under concurrent workloads, and physical-board
behavior remain unmeasured.

The parent closure/resource receipt SHA-256 values are
`dabf51e281ce6637f1775d0cf465c1f862d16d7288868e8b2efb7c054c2d3d6b`
and `41a4530881ec5e73a0fb4e6f16a65a99a5ce07c9f19cd2123d0e6cadffc634c0`.
The candidate values are
`352f2f2d98749f0e1a4be6199863a8748ef865962f4073c1f79d179944818810`
and `65446d0a3c8a5d84ded4f5d2909f54399a81ffbb9f0103a6684dcd8ba1b7d788`.
The comparison receipt is
`6fa5b308b417da89a998e926b692ed9ee9434a3d20121d27d3f5d9bcb8a390c7`.
The resource comparator deliberately reports the two positive linked-flash
deltas until this bounded exception receives review.

## Identity-only regular-file extension

The tar regular-file publication unit adds `O_IDENTITY = 0x00080000` to the
public flag vocabulary without adding or changing a syscall. `copen()` admits
that mode only with optional `O_NOFOLLOW`. `vn_open_at()` then retains only an
existing regular inode, records `FIDENTITY` without `FREAD` or `FWRITE`, skips
`openi()`, and releases namespace ownership. Directories and every data,
creation, truncation, append, search, execution, nonblocking, synchronization,
and locking combination are rejected before descriptor allocation or lookup.
The mode gives identity-conditioned rooted publication a descriptor that does
not depend on the target's read permission, including repeated tar members
whose first mode is `0200` or `0000`.

The ILP32 open fixture now executes 569 assertions. The new cases require
permission-independent regular identity, suppressed provider dispatch,
namespace release, directory refusal, and fourteen contradictory flag
classes. Seventeen calibrated mutations include one that restores provider
`openi()` for identity acquisition. Smaller C repeatedly includes `fcntl.h`
and checks the exact numeric value. The complete kernel ILP32 aggregate,
pathname-origin inventory, metadata, test-execution inventory, denominator,
and both exact target distributions pass.

The matched parent and candidate resource receipts report 108,728 to 108,800
PICO flash bytes and 96,252 to 96,316 PICO_UART flash bytes. Static SRAM
allocations and extents remain unchanged. Exact-command replay reports +4 text
bytes in each `ufs_syscalls.o` and +60 text bytes in each `vfs_vnops.o`;
`copen()` and `vn_open_at()` retain 152-byte and 24-byte static frames. The
parent closure/resource, candidate closure/resource, comparison, and
object/frame receipt SHA-256 values are
`cc746588cb410494f705f1839ac5680d4b65424a43eefd1c54f9138d3d32edb9`,
`08fbccbccfc79780b7ef0b2984eca978fabddea474811b240dd21b5776045981`,
`d1a79e9b571f6a3bbce497c3c1d0482905343eebccca013807f379954752cbfa`,
`7d4aa291cd4c672b1c1bd8f5281a062b90df88abfcac68e64875666c4b94fb60`,
`46bb1d91be5967140a952c2d4a8fd9427b07ead82720a04b3da4653a546621ac`,
and `78df80eaf7dfa434183676dbcb1e5149670882a4fcd4b263d096d8c11beb21d8`.
The comparer keeps the positive linked-flash delta open for bounded review.

## Residual namespace contract

Directory-relative lookup preserves the identity of the starting directory
through an ordinary operation. Parent components, absolute paths, absolute
symbolic links, and a concurrent rename can still move traversal outside an
archive root. The next archive repair therefore requires a separate contract
that serializes rename against beneath-root resolution and retains the
serialization through final create, link, remove, or metadata publication.
Identity-conditioned removal also needs the expected inode held through the
locked comparison and mutation. Tar creation and hard-link sources, cpio
cleanup, and compress publication/removal remain separate callers of that
later mechanism.

Persistence recovery, runtime peaks, MPU enforcement, deliberate faults, and
physical-board execution retain separate evidence states. The recorded
non-hardware gates do not establish those claims.
