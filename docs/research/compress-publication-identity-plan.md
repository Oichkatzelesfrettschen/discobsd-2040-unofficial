# Compress publication identity contract

`compress` currently opens an input pathname, checks and truncates an output
pathname, closes the output, applies metadata through the pathname, and then
removes the input through another pathname lookup.  A competing rename or
replacement can therefore redirect truncation, metadata, cleanup, or source
removal to an inode that the process did not open.

The repair keeps the codec tables and file format unchanged.  A named
operation acquires the input's parent directory, opens the input without
following its final symbolic link, writes a same-directory staged regular
file, applies ownership, mode, and timestamps to its descriptor, and publishes
that staged inode only if the destination still has the state that the caller
authorized.  Source removal and failed-stage cleanup compare the selected
entry with a retained descriptor before mutation.

## Additive publication interface

The public interface is:

```c
int publishat_beneath(int rootfd, const char *staged_path, int staged_fd,
    const char *target_path, int target_fd);
```

Both pathnames must be relative, nonempty walks beneath `rootfd`.  The final
parents must identify the same directory, and the staged descriptor must name
the regular file found at `staged_path`.  A `target_fd` value of `-1` authorizes
publication only while `target_path` is absent.  A nonnegative value authorizes
replacement only while `target_path` identifies the regular file retained by
that descriptor.  Any other negative value is invalid.

The kernel holds UFS namespace serialization from the first restricted lookup
through destination entry publication and staged-entry cleanup.  It retains
the staged and optional destination inodes through live descriptors, compares
the locked lookup results by inode identity, preserves sticky-directory and
immutable or append-only checks, and rejects cross-directory or cross-device
publication.  The interface never follows a symbolic link at either final
component.

Publication uses the existing rename invariant: the target entry always
contains either its authorized prior inode or the complete staged inode.  An
I/O error after the target update may leave the staged name as a second link;
the target still names the complete staged inode and the caller preserves its
input.  Failure before the target update leaves the target state unchanged.

## Utility ownership and failure preservation

For each named input, `compress` owns at most one search-only parent
descriptor, one retained input descriptor and its stream duplicate, one staged
output descriptor and its stream duplicate, and one optional destination
descriptor.  The retained input descriptor supplies file type, size, link
count, ownership, mode, and timestamps.  The staged descriptor receives
`fchown`, `fchmod`, and `futimes` before publication.  The utility deletes the
input only after publication succeeds, using `unlinkat_beneath()` with the
still-open input descriptor.

Write, flush, close, metadata, publication, input-removal, signal, allocation,
descriptor-table, and storage-exhaustion failures preserve the input.  Failed
staged output cleanup uses `unlinkat_beneath()` while the staged descriptor is
live.  A competing replacement at the staged or input name survives with an
`ESTALE` diagnostic.  A destination replacement after overwrite consent makes
publication fail instead of overwriting the replacement.

The existing `-c` standard-output mode retains ordinary named-input stream
behavior: it follows a final symbolic link and accepts multiple-link and
nonregular inputs when ordinary open and read operations permit them.  That
mode owns only the input descriptor and its stream duplicate; it does not
acquire parent, target, or staging publication resources.  The `-f` option
authorizes the destination inode opened before staging; it does not authorize
an arbitrary later replacement.  Interactive consent binds to the opened
destination descriptor used when the prompt was issued.

## Deciding controls

The kernel fixture must cover absent and expected destinations, source and
destination identity mismatches, final symbolic links, different parents and
filesystems, sticky directories, immutable and append-only objects, directory
and link-count reservation write failures, interrupted namespace-lock
acquisition, reference cleanup, and staged-source replacement after
destination publication.  Calibrated mutations discard the link-count
reservation error or remove the staged comparison, sticky-directory check,
destination comparison, rooted lookup, namespace serialization, same-parent
enforcement, or the first directory-write error.  Each mutation must reach its named
assertion after compiling with the production kernel warning profile.

The host utility fixture exercises named publication from a non-privileged
write-and-search-only parent.  It schedules input and destination replacement
after open, staged replacement during descriptor metadata and immediately
before publication, destination replacement immediately before publication,
staged replacement during cleanup, and input replacement immediately before
removal.  It separately injects `fwrite`, explicit `fflush`, output `fclose`,
input `fclose`, `fchown`, `fchmod`, `futimes`, publication, removal, cleanup,
allocation, descriptor-exhaustion, and signal-mask failures.  The combined
write and cleanup failure checks that the write diagnostic remains primary,
the cleanup diagnostic remains visible, the input survives, and the staged
entry remains when its conditional removal fails.  The signal fixture delivers
`SIGINT` immediately after staged-file creation, rejects an unmasked source
mutation, and injects both signal-mask block and restore failures.  Strict C17
and production GNU17 builds cover the exact utility source.  The host fixture
installs its interposition declarations after system headers and compiles that
boundary with `_FORTIFY_SOURCE=3`, because a fortified inline `openat()` may
otherwise bypass a command-line rename.  The source-replacement case requires
an explicit hook trace before its exit status can decide the identity claim.
A header mutation removes the post-header `openat` rename and must reproduce
the false success without reaching the replacement hook.

## Resource and evidence boundary

The comparison records both kernels' linked flash and static SRAM, affected
kernel objects and frames, the syscall wrapper and selected archive members,
`compress` text/data/BSS and loaded a.out occupancy, packed bytes and blocks,
root free blocks and inodes, and the maximum simultaneous descriptors and
inode references in the fixtures.  The approximately 30 KiB codec-table BSS
overlay remains unchanged.

The staged file temporarily requires the complete output allocation and one
additional directory entry.  Host and kernel fixtures establish the specified
namespace and failure behavior only for their exercised schedules.  Runtime
stack and heap high-water, persistent-media traffic, crash recovery, and
physical-board behavior retain separate evidence states.

## Matched static-resource evidence

Clean schema-2 distributions compare parent `5a5e075f` with the candidate
source snapshot built by the same bmake 20260912, Arm GCC 16.2.0, and GNU ld
2.47.20260726 toolchain.  The dependency closure accounts for 174 PICO and
166 PICO_UART C/header paths, every installed a.out, the native startup and
libc artifacts, generated inputs, selected archive members, and the packed
root.

| Constrained surface | Parent | Candidate | Delta | Candidate margin |
| --- | ---: | ---: | ---: | ---: |
| PICO flash bytes | 107,050 | 107,870 | +820 | 23,202 bytes below 128 KiB |
| PICO `.bss` / SwapRAM / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 | unchanged |
| PICO_UART flash / `.bss` bytes | 94,568 / 14,832 | 95,388 / 14,832 | +820 / 0 | 35,684 flash bytes below 128 KiB |
| `compress` text / data / BSS bytes | 12,116 / 232 / 30,328 | 13,484 / 244 / 30,552 | +1,368 / +12 / +224 | 44,280 loaded bytes; 103,176 bytes below 144 KiB |
| `compress` raw / packed bytes / packed blocks | 12,380 / 10,671 / 12 | 13,760 / 11,824 / 13 | +1,380 / +1,153 / +1 | one additional root block |
| Native board `libc.a` bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 | unchanged counts |
| Packed-root data / free blocks | 692 / 287 | 693 / 286 | +1 / -1 | 286 blocks free |
| Packed-root free inodes | 33 | 33 | 0 | unchanged |
| Every other shipped a.out and archive selection | measured baseline | unchanged | 0 | unchanged |

The comparer rejects the positive kernel, process, packed-file, and root-block
deltas and the changed `compress` archive selection by design.  The candidate
requests one bounded exception for the complete descriptor-owned publication
mechanism.  `compress` selects `dup.o`, `fchmod.o`, `fchown.o`, `fdopen.o`,
`futimes.o`, `getpid.o`, `openat.o`, `publishat_beneath.o`, `sigprocmask.o`,
and `unlinkat_beneath.o`; it drops `chmod.o`, `chown.o`, `freopen.o`, `stat.o`,
`unlink.o`, and `utimes.o`.  Every other selected archive member remains
unchanged.

Exact recorded production commands replayed with `-fstack-usage` give these
affected-object results:

| Object | Parent text/data/BSS | Candidate text/data/BSS | Delta |
| --- | ---: | ---: | ---: |
| `init_sysent.o` | 799/0/0 | 804/0/0 | +5/0/0 |
| `ufs_namei.o` | 4,984/0/40 | 4,988/0/40 | +4/0/0 |
| `ufs_syscalls.o` | 4,893/0/0 | 5,689/0/0 | +796/0/0 |
| `compress.o` | 4,911/32/30,216 | 6,185/40/30,440 | +1,274/+8/+224 |

`namei_at()` retains a 392-byte static frame and `direnter()` retains 72
bytes.  The new `publishat_beneath()` frame uses 136 bytes.  The optimized
`compress` main frame falls from 272 to 128 bytes; codec frames retain their
sizes.  The new interrupt-mask, descriptor-close, staged-discard, and terminal
mask-failure frames use 16, 16, 24, and 24 bytes.  Compiler static frames do
not establish a complete call-chain or runtime high-water bound.

A named publishing operation owns at most six nonstandard descriptors: the
parent, retained input, input stream duplicate, staged inode, staged stream
duplicate, and optional destination.  The six descriptors can retain four
distinct inodes before publication.  Fixed
descriptor and inode table limits remain unchanged.  Runtime pressure under
the complete workload remains open beside stack, heap, storage traffic,
persistence recovery, and physical-board behavior.
