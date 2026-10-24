# Rooted conditional unlink implementation plan

## Purpose

Archive cleanup currently verifies a pathname and removes it in separate
operations.  Another process can rename an ancestor or replace the final
directory entry between those operations.  The cleanup can therefore remove
an object that the archive process never created.  Ordinary `openat(2)` keeps
the starting-directory identity for one lookup, but it does not confine a
multi-component walk beneath that directory and it does not bind a later
removal to an already open file.

The first caller-complete repair adds
`unlinkat_beneath(rootfd, path, expected_fd)`.  The kernel serializes namespace
topology changes before acquiring an inode, resolves a restricted relative
path from `rootfd`, compares the final locked inode with the inode retained by
`expected_fd`, and removes the entry only when the identities match.  Cpio uses
the operation while its incomplete output descriptor remains live.  Tar
creation and replacement, cpio creation, and compress publication remain
separate later mechanisms.

## Interface contract

The public interface is:

```c
int unlinkat_beneath(int rootfd, const char *path, int expected_fd);
```

The call accepts one acquired directory descriptor, one user pathname, and
one descriptor for the regular file that the caller authorizes for removal.
The three arguments occupy three 32-bit syscall words.  Existing syscall
numbers and public layouts retain their values; the generated syscall tables
append the new entry after `openat(2)` at slot 158.

The operation applies these rules before publication:

- `path` must be a nonempty relative pathname.
- Every component must be nonempty and must differ from `.` and `..`.
- Every component, including the final component, must be a nonsymbolic link.
- A traversal that changes mounted filesystem returns `EXDEV`.
- `rootfd` must identify a searchable directory.
- `expected_fd` must identify a regular inode and retain that inode until the
  comparison completes.
- The final directory entry must resolve to the same live inode held by
  `expected_fd`; a different inode returns `ESTALE` and remains untouched.
- Existing directory write, sticky-directory, immutable, append-only,
  read-only filesystem, and mounted-object checks retain their precedence.
- The operation removes a regular-file entry only.  Directory removal remains
  owned by `rmdir(2)`.

The acquired root inode defines the root identity.  Renaming or detaching the
root descriptor before a later call does not retarget the descriptor.  During
one call, namespace serialization prevents an ancestor rename from changing
the resolved ancestry before the final mutation.  The contract establishes
namespace identity for the exercised UFS operation.  It does not establish
concurrent file-content consistency, media-write atomicity, crash recovery, or
power-loss durability.

## Locking and ownership

One private sleepable UFS namespace gate owns topology serialization.  The
gate adds two fixed bytes or words of kernel state, depending on the compiler's
chosen scalar representation; the resource receipt records the linked result.

- A syscall acquires the gate before `namei()` or `namei_at()` can lock an
  inode.
- A waiter uses `tsleep(..., PINOD | PCATCH, 0)` and returns `EINTR` or
  `ERESTART` explicitly.  The gate never uses interruptible `sleep()`, whose
  compatibility path can leave through `longjmp()`.
- Legacy `rename(2)` holds the gate through source lookup, destination lookup,
  destination publication, source relookup, source removal, and cleanup.
- `unlinkat_beneath()` holds the gate through restricted lookup, expected-inode
  comparison, `dirremove()`, link-count publication, and reference cleanup.
- Every inode lock and temporary reference is released before the namespace
  gate is released.
- File-data transfer, device open or close, advisory locking, and user-space
  execution occur outside the gate.

The RP2040 kernel is uniprocessor.  Only process context changes this gate.
The wait loop follows the existing inode-lock sleep discipline: another
process cannot release the gate between the busy test and `tsleep()` queue
insertion because the current process retains the processor until it enters
the scheduler.  Interrupt handlers neither acquire nor release the gate.

Historical privileged directory hard links and directory `unlink(2)` can
change directory ancestry without `rename(2)`.  `link(2)` therefore rejects a
directory source for every credential, and `unlink(2)` rejects a directory
target for every credential.  `mkdir(2)` and `rmdir(2)` retain their existing
contracts; `rmdir(2)` can remove only an empty directory and cannot relocate a
live descendant.

`namei_at()` owns every inode acquired during traversal.  On successful
`DELETE | LOCKPARENT` lookup, it returns the final inode and its parent locked.
`dirremove()` retains the parent lock and reference; the syscall releases both
objects.  The `dirremove()` provider must return failure and preserve link
counts when either its first-entry `rdwri()` or its coalesced-entry `bwrite()`
fails.

## Falsifiers and calibrated controls

The kernel fixtures must reject these production-source mutations:

1. Ignore the first-entry `rdwri()` result or the coalesced-entry `bwrite()`
   result in `dirremove()`.
2. Acquire namespace serialization after the first inode lookup.
3. Release serialization before `dirremove()`.
4. Let legacy `rename(2)` bypass serialization during its second source
   lookup.
5. Accept an absolute path, empty component, `.`, or `..`.
6. Follow an intermediate or final symbolic link.
7. Continue after a mounted-filesystem transition.
8. Compare device and inode numbers after dropping the expected descriptor
   rather than comparing the retained inode identity.
9. Remove the final entry after an `ESTALE` identity mismatch.
10. Permit `link(2)` or `unlink(2)` to mutate a directory entry as a directory
    topology operation.
11. Release namespace serialization before a relocated directory passes
    `checkpath()`, destination relookup, `..` rewriting, and source removal.

The fixtures exercise a gate waiter that wakes, a signal-interrupted waiter,
invalid expected and root descriptors, an empty restricted path, sticky,
append-only and immutable refusals, first-entry and coalesced-entry write
failures, identity mismatch, successful single- and multi-component removal,
cold and populated name-cache routes, and balanced reference and lock
ownership.  The cpio host fixture schedules a
replacement before its cleanup provider acquires the name and requires the
replacement to survive.  A separate production-kernel fixture establishes the
comparison and mutation under the actual parent lock; the host shim alone does
not establish kernel atomicity.

## Matched resource evidence

Clean schema-2 distributions compare parent `0938a979` with candidate
`0c4dee43`.  Both captures used bmake 20260912, arm-none-eabi-gcc 16.2.0,
GNU ld 2.47.20260726, and Python 3.14.8.  The dependency closures account for
174 PICO and 166 PICO_UART C/header paths plus every installed a.out, native
startup and libc artifact, generated input, selected archive member, and the
packed root.  Cpio's closure moves from 82 to 81 C/header paths because its
identity-conditioned cleanup removes the `strrchr.o` and `unlink.o`
dependencies and selects `unlinkat_beneath.o`.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 106,382 | 107,050 | +668 | 24,022 bytes below 128 KiB |
| PICO `.bss` / SwapRAM / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 93,908 / 14,832 | 94,568 / 14,832 | +660 / 0 | 36,504 flash bytes below 128 KiB |
| Cpio text / data / BSS bytes | 10,292 / 200 / 884 | 10,384 / 200 / 884 | +92 / 0 / 0 | 11,468 loaded bytes |
| Cpio raw / packed bytes / packed blocks | 10,524 / 8,770 / 10 | 10,616 / 8,852 / 10 | +92 / +82 / 0 | unchanged block count |
| Native board `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 | unchanged |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every other shipped a.out and archive selection | measured baseline | unchanged | 0 | unchanged |

The comparer rejects the positive flash and cpio deltas and the changed cpio
archive selection by design.  The candidate requests a bounded exception for
the caller-complete namespace identity mechanism.  Cpio adds the 20-byte
`unlinkat_beneath.o` wrapper while dropping `strrchr.o` and `unlink.o`; every
other selected member remains unchanged.  The exception adds zero linked
static SRAM, process-window allocation, packed-root blocks, inodes, or native
library bytes.

Exact recorded production commands replayed with `-fstack-usage` and
reproducible path maps produce the following affected-object results in both
kernel configurations:

| Object | Parent text/data/BSS | Candidate text/data/BSS | Delta |
| --- | ---: | ---: | ---: |
| `init_sysent.o` | 794/0/0 | 799/0/0 | +5/0/0 |
| `ufs_namei.o` | 4,608/0/36 | 4,984/0/40 | +376/0/+4 |
| `ufs_syscalls.o` | 4,633/0/0 | 4,893/0/0 | +260/0/0 |
| `cpio.o` | 3,540/0/776 | 3,652/0/776 | +112/0/0 |
| ARM ELF and a.out syscall wrappers | absent | 20/0/0 each | +20/0/0 each |

`namei_at()` retains a 392-byte static frame, `dirremove()` retains 40 bytes,
`rename()` retains 192 bytes, and `unlink()` retains 120 bytes.  The new
namespace enter and exit functions use 16 and 8 bytes; the two gate flags are
one byte each, with the object growing four BSS bytes after alignment.  The new
`unlinkat_beneath()` frame is 120 bytes.  Cpio's `main()` and `copy_in()`
frames retain 32 and 128 bytes, while `remove_incomplete_output()` uses 24
bytes.  Compiler frames do not establish complete call-chain or runtime stack
high-water bounds.

Cpio holds one extraction-root descriptor and one output descriptor at the
cleanup call, in addition to its standard streams.  The mechanism allocates no
new descriptor-table slot. The syscall temporarily owns one reference copied
from the expected output inode while `namei_at()` owns its current and prior
traversal inodes. Including the extraction-root and output descriptor
references, a nested walk can hold five inode references across four distinct
inodes. The calibrated `a/b/leaf` cold- and populated-cache cases observe that
bound. Fixed descriptor and inode table limits remain unchanged.

| Receipt | SHA-256 |
| --- | --- |
| Parent clean execution | `96a8544fb0d6fc70220a516a1585d61f0629ce32d488b488b3cc891cec213909` |
| Parent distribution execution | `c2b1d5ab641e9cf804fe6915fc977ad5d720811ffc4926ad2d816f25753d709c` |
| Parent source snapshot | `bde7cf181c82f5786037e8941ed2d4afe45b4da06ff0322335e8c90c674f1360` |
| Parent dependency closure | `dfd7d440bcf14c961205f20fbb19ab8fe8998425474b2ee16acfe4973b30a738` |
| Parent schema-2 resources | `7c5b6251a62c5eded8de17d91b94467023ab33581e698cad3d679ebe8baa1f99` |
| Parent exact-command objects and frames | `5c5f68c4931d116437fc7d10837a49d080838dc6280a6bdd6adec77f1aeaeca2` |
| Candidate clean execution | `32177fb2bc8f2c0449002d1dc681bad405959ea82871008e73c00480b6a46f66` |
| Candidate distribution execution | `4fdd7bbaebb124fc27082169d68c3eba6895569bdc07a6dc795ab4e854ac6c55` |
| Candidate source snapshot | `ad2d94e0bee94c15469143a8a62e85830452601847720dfa16f6e3b9ddde96bd` |
| Candidate dependency closure | `c26b631ab4447ed97c45af09ab8e9a41d9a6c123e83e5227ca4f7582f8033010` |
| Candidate schema-2 resources | `f54addcc1f44aaece7f10a268fa2ec892dff0275bd02431cf4a9c2a7bff15642` |
| Candidate exact-command objects and frames | `23b082ad399af8941675af3beee248dd63fe4be07b4115cac4533b85bf8e5106` |
| Static-resource comparison | `1dece889fd5d171f2ca7aca48f0ebb6a60abf12a3458b274f0e9f72907864a27` |

Runtime stack and heap high-water, storage traffic, persistence recovery, and
physical-board behavior remain unmeasured.  Each state stays open rather than
borrowing acceptance from the static comparison.

## Ordered tasks

1. [x] Add a failing `dirremove()` write-error fixture for both directory-entry
   layouts, then make the production provider publish only successful writes.
2. [x] Add a failing namespace-gate fixture for free acquisition, wakeup,
   explicit interruption, and balanced release, then implement the private
   gate.
3. [x] Add failing legacy-topology fixtures, then hold the gate across all
   `rename(2)` exits and reject directory `link(2)` and `unlink(2)` calls.
4. [x] Add failing restricted-lookup fixtures for pathname syntax, symbolic
   links, mounted-filesystem changes, and ownership cleanup, then add the
   bounded `namei_at()` mode.
5. [x] Add failing syscall fixtures for descriptor validation, lock order,
   expected-inode mismatch, provider errors, and success, then implement
   `unlinkat_beneath()`.
6. [x] Add generated-metadata, public-prototype, function-pointer, Smaller C,
   and archive-selection failures, then append syscall 158 and its libc member.
7. [x] Add failing cpio cleanup and replacement fixtures, then keep the output
   descriptor live through identity-conditioned cleanup of truncated input,
   write failure, and the target kernel's regular-file close path.
8. [x] Register the focused gates with the root Makefile, testing guide,
   execution inventory, and CI owner.  Refresh the source denominator and
   correct the testing guide's stale source-state counts.
   The denominator delta contains the four kernel fixtures and the cpio shim
   source/header as one six-path accepted test mechanism.  Existing Linux and
   macOS `check-host` jobs own the two registered cpio execution variants.
9. [x] Run focused strict-C17, GNU17, ILP32, ABI, Smaller C, cpio, mutation,
   and generated-file gates.  Rebuild every public-header and libc consumer
   from a clean tree.
   The exact PICO/PICO_UART production compiles, focused 32-bit fixtures,
   generated metadata, 19-program Smaller C suite, damaged-archive control,
   strict-C17 cpio suites, and clean libc/cpio target rebuilds pass.
10. [x] Compare clean parent and candidate distributions for PICO and PICO_UART.
    Record kernel flash and SRAM, affected object and frame sizes, cpio a.out
    occupancy, libc archive selection, packed-root bytes and blocks, and the
    maximum simultaneous descriptor and inode ownership.  Positive constrained
    growth requires a bounded reviewed exception.
    The matched schema-2 receipts and exact-command replay above measure every
    declared static surface.  The candidate requests the bounded growth and
    archive-selection exception; pull-request review remains part of task 11.
11. [ ] Run `logged-check`, review the staged diff and a fresh-context branch
    review, push the focused branch, open and merge a pull request with a merge
    commit, synchronize `main`, and retire the clean worktree.
    Fresh-context review found no critical implementation defect. Its directory
    relocation and nested-lookup coverage gap is closed by a 138-assertion
    topology fixture, a fifth topology mutation, and 756 rooted-unlink
    assertions over cold and populated name-cache routes. Follow-up review
    reports no critical or important finding. The corrected focused gates pass.
    The exact-head aggregate at `b55c0255` completed both stages on 2026-10-24:
    the clean build passed in 61.8 seconds, the non-hardware check tier passed
    in 356.6 seconds, and the complete invocation passed in 418.417 seconds.
    Invocation `rp2040-check-local-20261024T153947Z-3373339-1601499f` has status
    SHA-256
    `9d4e058530f13593d3ae8d5107ff2a898536da4da44da96a50c5c7eb58faad20`.
    Publication, exact-head CI, integration, synchronization, and worktree
    retirement remain open.

Physical-board execution, flash programming, serial access, and persistence
fault injection remain outside this unit and require separate authorization.
