# Bounded userland I/O and secret-memory primitives

The bounded-userland backport ports five modern BSD mechanisms into the
RP2040 tree without importing the dependency surfaces that make the upstream
programs too large:
robust `tee` writes, unbounded-identity `du` hard-link accounting, a strict
`resize` reply parser, `explicit_bzero`, and `timingsafe_bcmp`. The build at
the changed tree uses one fewer root-filesystem block than the exact-base
build while carrying both new libc functions in the normal ELF library, the
full a.out library, and the reduced board-native a.out library.

The comparison uses base commit
`07a8e3a27c96826e4945d3f2e19f15af40cd147d`. Both sides run a clean
`bmake MACHINE=rp2040 distribution` with bmake 20260824, Arch
arm-none-eabi-gcc 16.2.0, and host GCC 16.2.1 20260810. The result establishes
cross-build behavior and artifact size. The result does not establish board
execution or a timing bound on physical hardware.

## Source provenance

The source survey reads four sibling BSD checkouts at these commits:

| Source tree | Commit | Relevant files |
| --- | --- | --- |
| NetBSD | `1e8e727e461bb760bf82162f33b64afea02da2f3` | `usr.bin/tee/tee.c`, `usr.bin/du/du.c`, `usr.bin/resize/resize.c` |
| OpenBSD | `8849c5d46b9b299f5d887033706f1ddd60f8f966` | `usr.bin/tee/tee.c`, `usr.bin/du/du.c`, `lib/libc/string/explicit_bzero.c`, `lib/libc/string/timingsafe_bcmp.c` |
| FreeBSD | `88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da` | `usr.bin/tee/tee.c`, `usr.bin/du/du.c`, `lib/libc/string/timingsafe_bcmp.c` |
| DragonFly BSD | `54d7118100cb99b741fe92cfd8d542c623b6f8d0` | `usr.bin/tee/tee.c`, `usr.bin/du/du.c`, `lib/libc/string/explicit_bzero.c`, `lib/libc/string/timingsafe_bcmp.c` |

The port preserves mechanisms rather than copying whole programs. Modern
`tee` supplies the `O_APPEND`, partial-write, and accumulated-status
contracts. OpenBSD `du` supplies the key hard-link invariant: a record stores
the number of unseen names and leaves memory after the last expected name.
The modern `du` programs depend on `fts(3)`, tree macros, locale helpers, and
formatting libraries that DiscoBSD does not carry. The local traversal
retains the small directory-stream design and adds only the invariant.

NetBSD `resize` still uses `sscanf`; the local parser deliberately departs
from it. An xterm cursor-position reply has the finite grammar
`ESC '[' rows ';' columns 'R'`, with each number in 1 through 999. A direct
parser expresses that grammar in one pass and prevents the utility multicall
binary from pulling in `_doscan`, `scanf`, `sscanf`, and `_ctype_`.

OpenBSD and DragonFly BSD implement `explicit_bzero` as `memset` plus a weak
hook. The RP2040 a.out toolchain has no useful weak-hook optimization barrier,
so the local function writes through a volatile byte pointer. The OpenBSD
`timingsafe_bcmp` OR-of-XOR reduction transfers directly; volatile input
pointers make the two loads part of the observable execution at every byte.

## `tee`: fixed storage with complete writes

`tee` owns a 1,024-byte transfer buffer and a 20-entry output table. Standard
output occupies the first entry, so at most 19 named outputs open at once.
The table costs fixed BSS and never allocates. Each read retries `EINTR`.
Each write advances by the returned positive count, retries `EINTR`, treats a
zero-byte write as `EIO`, and continues serving unaffected destinations after
one destination fails. Named descriptors close on both the failure path and
the normal exit path. Open, read, write, and close failures contribute to the
exit status.

`O_APPEND` provides the append contract at the kernel boundary. A separate
`lseek` followed by a write leaves a race window between the two operations;
the flag makes each write choose the then-current end of file atomically.
Grouped `-ai`, `--`, and a literal `-` filename remain accepted.

The old program carried two 8,192-byte arrays and copied every byte from one
to the other. The new program carries one 1,024-byte array. Its a.out resident
image changes from 18,080 to 2,756 bytes even though the raw executable grows
44 bytes. The coefficient is therefore dominated by BSS:

```text
resident_delta = text_delta + data_delta + bss_delta
               = (1676 - 1548) + (8 - 92) + (1072 - 16440)
               = -15324 bytes
```

The extra control-flow text buys defined short-write and failure behavior;
the removed staging storage buys the process-window reduction.

## `du`: live hard-link identities instead of a fixed ceiling

The former `du` records at most 1,000 `(device, inode)` pairs in a static
array. It also scans through index `mlx` inclusively, which reads one element
past the array when all 1,000 slots fill. The replacement uses a linked record
for every live hard-link identity:

```text
remaining_on_first_name = st_nlink - 1
remaining_on_repeat     = remaining - 1
release_condition       = remaining_on_repeat == 0
```

The list has no identity-count ceiling. Its peak allocation follows the
number of incomplete link sets encountered during the traversal rather than
the number of hard-linked inodes ever seen. A directory tree that presents
all names of each inode close together releases records early. An incomplete
set, including links outside the operand, remains until the operand completes
and then leaves through `free_hard_links`.

Allocation exhaustion has a defined conservative result. `du` emits one
diagnostic, releases the existing records, disables further tracking, counts
later names separately, and returns failure. The total can overcount storage,
but it cannot silently omit file blocks because an allocation failed.

One `DU_PATH_SIZE` buffer replaces the old `path[BUFSIZ]` plus
`name[BUFSIZ]` pair. Component appends reserve the terminator before writing.
The unwind restores the exact operand prefix, including a trailing slash.
The traversal reports over-capacity operands and descendants. Multiple
operands retain separate working directories through the historical child
process boundary, and the parent propagates every child failure. `waitpid`
retries `EINTR`.

The traversal closes a parent directory before descending so directory depth
never consumes the process descriptor table. POSIX binds a `telldir()` cookie
to the same directory stream passed to `seekdir()`; the historical code closed
that stream and applied its cookie to a new one. The replacement reopens the
parent, scans through the unique child name just processed, and resumes at the
following entry. This preserves one-stream ownership without relying on the
cross-stream behavior that Linux happens to accept and macOS leaves
unspecified. A calibrated symbol-closure gate rejects both directory-cookie
functions from the exact host object.

The resident image changes from 18,264 to 9,720 bytes. The 9,020-byte BSS
reduction exceeds the 476-byte text growth, so the process window gains 8,544
bytes while hard-link capacity becomes dynamic.

## `resize`: grammar replaces general scanning

The reply parser accepts exactly two decimal fields in 1 through 999. It
rejects signs, spaces, empty fields, zero, fourth digits, alternate
delimiters, suffixes, and trailing bytes. A 16-byte reply buffer suffices:
two escape introducer bytes, three row digits, one semicolon, three column
digits, one `R`, and one terminator require 11 bytes.

The direct parser composes with ASCII predicates in `cmp`, `more`, `sort`,
and `uniq`. These small local expressions eliminate the `_ctype_` table from
`utilbox`; removing `sscanf` also eliminates the general scanner. The final
temporary ELF closure rejects `_ctype_`, `_doscan`, `scanf`, and `sscanf`.
The stripped a.out carries no symbol table, so the gate inspects the linked
ELF immediately before conversion rather than pretending that post-strip
absence proves anything.

The closure reduction supplies the highest byte leverage in the batch.
`utilbox` loses 1,776 raw and resident bytes and 1,489 packed bytes. The
individual direct predicates are small; the removed archive-member closure
is large. Linkage granularity, rather than source-line count, determines the
coefficient.

## Explicit erasure and content-independent comparison

`explicit_bzero` writes every byte through a volatile pointer. The C17
abstract machine therefore exposes each store as an observable side effect,
which blocks dead-store elimination after the last source-level use of a
password. The Thumb-1 function occupies 16 text bytes.

`timingsafe_bcmp` loads both input bytes on every iteration and accumulates
their XOR values with bitwise OR. Its iteration count depends on `length`,
while its control path does not depend on buffer contents. The Thumb-1 object
occupies 28 text bytes. Disassembly contains two volatile byte loads and one
conditional length branch and contains no call to `memcmp` or `bcmp`.

The comparison contract returns zero for equal buffers and a nonzero value
for different buffers. It does not promise an ordering. Callers compare
stored password lengths before calling it, because equal work for unequal
lengths would otherwise require a separate maximum-length storage contract.

`login`, `passwd`, and `uucpd` use the comparison for password-hash equality.
`login`, `passwd`, and `uucpd` erase plaintext input after use. `crypt` erases
its DES key schedule and working arrays before returning the static output.
`passwd` copies at most the eight significant characters used by the legacy
DES password format and erases both the retained copy and every `getpass`
buffer path.

The normal libc, full a.out libc, and reduced board libc each carry exactly
one object for both primitives. `libc-sink.c` names both calls, so the reduced
archive's source-of-truth closure includes them. The reduced archive changes
from 144 members and 53,544 bytes to 146 members and 53,910 bytes.

`uucpd` remains outside the shipped RP2040 program set. Its source receives
the same secret-handling contract, while the normal tree gates do not claim a
standalone `uucpd` build: that legacy translation unit requires a selected
`BSD4_2` or `BSD2_9` configuration and carries unrelated pre-C17 declarations.

## Final artifact accounting

`tools/bin/size` reads the OMAGIC segment headers. `tools/bin/hsaout -s`
packs each executable with the shipping codec and applies the filesystem
allocation formula. The filesystem has four direct block addresses and then
indirect blocks, so the measured executable set uses:

```text
data_blocks = ceil(bytes / 1024)
charged_blocks = data_blocks + (data_blocks > 4)
```

The larger double- and triple-indirect terms stay inactive for every artifact
in the measured executable set.

| Artifact | Resident delta | Raw delta | Packed delta | Packed block delta |
| --- | ---: | ---: | ---: | ---: |
| `tee` | -15,324 | +44 | +122 | 0 |
| `du` | -8,544 | +476 | +354 | 0 |
| `utilbox` | -1,776 | -1,776 | -1,489 | -1 |
| `login` | +188 | +188 | +171 | 0 |
| `passwd` | +360 | +360 | +311 | 0 |
| Five executable images | -25,096 | -708 | -531 | -1 |

The board libc is an uncompressed archive, not a process image. Its 366-byte
growth stays at 54 charged blocks on both sides: 53 data blocks plus one
single-indirect block. The combined shipped-payload delta is therefore:

```text
root_payload_delta = executable_packed_delta + board_libc_raw_delta
                   = -531 + 366
                   = -165 bytes
```

`fsutil --check --partition=1` independently reports the exact-base image at
856 used blocks and 123 free blocks, and the changed image at 855 used blocks
and 124 free blocks. The fixed-size `sdcard.img` files remain equal in outer
length; the free-list observation establishes the recovered block.

## Reproduction and falsifiers

The focused behavior and target-shape checks run with:

```sh
bmake MACHINE=rp2040 check-tee-contracts
bmake MACHINE=rp2040 check-tee-contracts-cross
bmake MACHINE=rp2040 check-du-contracts
bmake MACHINE=rp2040 check-du-contracts-cross
bmake MACHINE=rp2040 check-resize-contracts
bmake MACHINE=rp2040 check-resize-contracts-cross
bmake -C tests/libc_contracts check-string-security
bmake -C tests/libc_contracts check-string-security-cross
bmake -C tests/libc_contracts check-aout
bmake MACHINE=rp2040 distribution
tools/bin/hsaout -s usr.bin/tee/tee usr.bin/du/du \
    sbin/utilbox/utilbox usr.bin/login/login usr.bin/passwd/passwd
tools/bin/fsutil --check --partition=1 distrib/rp2040/sdcard.img
```

The `tee` contract injects `EINTR`, 13-byte short writes, zero-byte writes,
open failures, write failures, close failures, and descriptor exhaustion. A
lost byte, an early stop of an unaffected output, or a success status after an
I/O failure falsifies the implementation.

The `du` contract exercises ordinary, `-a`, and `-s` output; 1,001 distinct
simultaneously live hard-linked identities; incomplete link sets; forced
allocation exhaustion; multiple operands; trailing-slash restoration; and
both sides of the path boundary. A fixture that imports `telldir` and `seekdir`
calibrates the directory-resume closure. A duplicate count with tracking
active, an omitted count after allocation failure, an accepted over-capacity
path, a swallowed child failure, or either directory-cookie symbol in the
exact object falsifies the implementation.

The `resize` contract spans all accepted boundary values and malformed field,
delimiter, prefix, suffix, and range classes. A closure containing `_ctype_`
or any scanner symbol falsifies the byte-saving claim.

The string contract verifies zero-, partial-, and full-length erasure and
first-, middle-, and final-byte differences. A data-dependent conditional
branch in `timingsafe_bcmp`, fewer than two byte loads per loop, delegation to
an early-return comparison, or either object missing from an a.out archive
falsifies the security contract. An early-return comparison object calibrates
the disassembly rejection path; an object naming every forbidden utilbox
symbol calibrates the closure rejection path. A different base commit,
compiler, flags, or packing codec invalidates the numerical size comparison
and requires a fresh pair of clean builds.
