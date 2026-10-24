# Tar descriptor-owned timestamp identity

Tar retained an open descriptor for each extracted regular file and for every
directory whose deferred metadata it restored, but timestamp publication used
the pathname after a separate identity check. A concurrent rename could move
the extraction-owned object and install a replacement at the same pathname.
The pathname `utimes(2)` call then changed the replacement while leaving the
object created from the archive with the wrong timestamp.

The exact-source host fixture interposes the timestamp calls at the
publication seam. The interposer renames the extracted object, creates a
same-kind replacement at the original pathname, and then permits publication.
The pathname implementation fails because the moved object does not receive
the archive timestamp. The descriptor implementation timestamps the moved
object and preserves the replacement timestamp and file contents. The fixture
executes this schedule for a regular file and a deferred directory.

## Additive syscall and locking contract

Syscall 156 adds `futimes(int, const struct timeval [2])`. Existing syscall
numbers, argument records, public layouts, and calling conventions retain
their values. The generated public number, private dispatch declaration,
handler table, name table, ARM libc wrapper, a.out libc wrapper, and selected
tar archive member all derive from the same syscall master. The board-libc
classifier recognizes the wrapper, while the measured `boardlibc-members`
closure does not select either timestamp wrapper.

The kernel stages the complete two-element `timeval` array before descriptor
lookup. A failed copy returns its error without consulting or locking a
descriptor. A null timestamp pointer uses the current kernel time and sets
`VA_UTIMES_NULL`; an explicit pair clears that flag and publishes the staged
access and modification seconds. Descriptor lookup returns the existing
`EBADF` or non-inode error. A successful lookup locks the inode across
`ufs_setattr()` and unlocks the inode after success or failure. Permission,
read-only filesystem, and inode-update behavior remain owned by
`ufs_setattr()`.

`utimes(2)` uses the same timestamp preparation helper. The refactor preserves
its existing explicit and null-pointer behavior while removing duplicated
copying and attribute initialization.

## Tar callers

Regular-file extraction calls the descriptor timestamp helper after the data
write and optional mode update, then records provenance and closes the same
descriptor. Deferred directory restoration opens and verifies the directory,
calls the same helper, applies the mode through `fchmod(2)`, and closes the
descriptor. Error messages retain the archive pathname for the operator while
the mutation targets the descriptor-owned inode.

The change removes the remaining pathname timestamp helper. Tar continues to
retain one descriptor at a time during publication; the change adds no
per-member descriptor list or persistent allocation.

## Calibrated verification

`tests/kernel/futimes_test.c` links the production syscall at the target's
ILP32 width and executes 55 assertions. The fixture covers complete copying,
null timestamps, copy failure before descriptor lookup, invalid descriptor
preservation, inode locking and unlocking, successful publication, and
`ufs_setattr()` error propagation. Two warning-clean source mutations copy one
`timeval` or perform descriptor lookup before copying; each must fail its named
assertion.

`bin/tar/tests/timestamp_identity_test.sh` compiles the exact tar source under
strict C17 warnings and runs the regular-file and directory replacement
schedules. A generated pathname mutation restores `utimes(2)` at the helper
and must fail with `moved object did not receive the archive timestamp`.

The syscall metadata gate pins slot 156, two argument words, 157 dispatch
slots, generated-file freshness, and PICO/PICO_UART compilation. Smaller C
repeatedly includes `sys/time.h`, checks the function-pointer type, and requires
the selected `futimes.o` archive member. Exact PICO and PICO_UART distribution
builds compile and link the kernel, libc, board libc, and tar callers.

## Static resource state

Matched clean schema-2 distributions compare parent `82ce6359` with the staged
candidate under bmake 20260912, arm-none-eabi-gcc 16.2.0, and GNU ld
2.47.20260726. Their dependency closures account for both kernel
configurations, every installed a.out, the packed root, and the native startup
object and libc archive.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,826 bytes | 105,990 bytes | +164 bytes |
| PICO_UART flash | 93,352 bytes | 93,508 bytes | +156 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| `ufs_syscalls2.o` text / data / BSS | 979 / 0 / 0 bytes | 1,119 / 0 / 0 bytes | +140 / 0 / 0 bytes |
| `utimes()` symbol / static frame | 124 / 160 bytes | 132 / 160 bytes | +8 / 0 bytes |
| `futimes()` symbol / static frame | absent | 132 / 56 bytes | +132 / 56 bytes |
| `tar.o` text / data / BSS | 14,024 / 58 / 2,181 bytes | 13,813 / 58 / 2,181 bytes | -211 / 0 / 0 bytes |
| `storedirmetadata()` static frame | 192 bytes | 168 bytes | -24 bytes |
| descriptor timestamp helper static frame | absent | 32 bytes | +32 bytes |
| Tar raw / loaded bytes | 27,880 / 31,768 | 27,696 / 31,584 | -184 / -184 bytes |
| Tar packed bytes / blocks | 23,154 / 24 | 22,990 / 24 | -164 / 0 |
| Native board libc | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

The production optimizer inlines `prepare_utimes_attributes()` into the two
syscall entries, so the object contains no independently measurable helper
symbol or frame. The ARM ELF and a.out `utimes.o` and `futimes.o` wrappers each
contain 20 text bytes. Tar's selected libc set replaces `utimes.o` with
`futimes.o`; the comparer reports the added member even though the selected
member count and wrapper size remain constant. The full ELF and a.out libc
archives gain the additive wrapper, while the installed board libc remains
byte-identical because its measured native closure calls neither timestamp
interface.

The bounded exception accepts the kernel flash increase and tar archive-member
replacement for descriptor-owned timestamp publication. The candidate retains
25,082 PICO and 37,564 PICO_UART bytes below the 128 KiB kernel limit, reduces
tar's loaded and packed bytes, preserves every static SRAM surface, and keeps
the packed root at 24 tar blocks and 692 total data blocks. Every other shipped
a.out measurement remains unchanged.

Runtime kernel stack, process stack, heap high-water, storage traffic, and
physical-board behavior remain unmeasured.

## Remaining namespace contracts

The descriptor closes timestamp identity only while tar owns the open object.
Tar still performs pathname-separated creation, hard-link source lookup,
replacement removal, symlink publication, and parent traversal. Cpio cleanup
and compress metadata, publication, and removal retain equivalent final-leaf
identity gaps.

Directory-relative lookup can anchor an acquired parent, but a status check
followed by a pathname mutation still races on the final component. The next
archive units therefore need an explicit namei starting-directory contract and
identity-conditioned mutation under the parent lock, or a separately specified
staged-publication design. Moving an opened directory outside the extraction
root also distinguishes descriptor identity from confinement beneath the
original root.

Host replacement fixtures establish the exercised schedule. They do not prove
arbitrary concurrent namespace behavior, persistence recovery, or silicon
execution. A physical-board run requires separate authorization for the exact
image and workload.
