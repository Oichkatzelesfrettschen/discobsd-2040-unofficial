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

The fixtures exercise a gate waiter that wakes, a signal-interrupted waiter,
every lookup and descriptor error, sticky and immutable refusals, first-entry
and coalesced-entry write failures, identity mismatch, successful removal, and
balanced reference and lock ownership.  The cpio host fixture schedules a
replacement before its cleanup provider acquires the name and requires the
replacement to survive.  A separate production-kernel fixture establishes the
comparison and mutation under the actual parent lock; the host shim alone does
not establish kernel atomicity.

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
10. [ ] Compare clean parent and candidate distributions for PICO and PICO_UART.
    Record kernel flash and SRAM, affected object and frame sizes, cpio a.out
    occupancy, libc archive selection, packed-root bytes and blocks, and the
    maximum simultaneous descriptor and inode ownership.  Positive constrained
    growth requires a bounded reviewed exception.
11. [ ] Run `logged-check`, review the staged diff and a fresh-context branch
    review, push the focused branch, open and merge a pull request with a merge
    commit, synchronize `main`, and retire the clean worktree.

Physical-board execution, flash programming, serial access, and persistence
fault injection remain outside this unit and require separate authorization.
