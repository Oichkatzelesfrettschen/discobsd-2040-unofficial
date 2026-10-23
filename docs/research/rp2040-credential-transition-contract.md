# RP2040 credential admission and publication contract

The kernel credential handlers previously used old-style definitions while
the generated syscall dispatch interface declared each handler as
`void (void)`.  The privilege predicate also lived in the filesystem
translation unit even though credential, signal, process, time, resource,
mount, sysctl, and device paths consume it.  Host fixtures substituted local
copies of that predicate, so they could pass while the production symbol or
its declaration boundary was broken.

`sys/kern/kern_prot.c` and `sys/kern/kern_prot2.c` now define every function
with a prototype.  `suser(void)` is owned by `kern_prot2.c` and declared by
`sys/systm.h`, the kernel service interface already included by its callers.
The symbol, privilege rule, syscall numbers, argument counts, calling
conventions, public types, and credential layouts remain unchanged.

## Credential contracts

The effective user ID decides privilege.  `suser()` returns one for effective
UID zero.  Every refusal returns zero and records `EPERM` in the current
u-area.  The scalar setters finish admission before their first credential
write.  Their remaining assignments cannot fail.

| Interface | Admission | Publication |
| --- | --- | --- |
| `setuid(uid)` | The requested UID equals the real UID, or `suser()` admits the caller. | Assign the process-table UID and effective, real, and saved UIDs. |
| `seteuid(euid)` | The requested UID equals the real or saved UID, or `suser()` admits the caller. | Assign both copies of the effective UID: `u_uid` and `p_uid`. Preserve real and saved UIDs. |
| `setgid(gid)` | Reject `NOGROUP` with `EINVAL`; otherwise the requested GID equals the real GID, or `suser()` admits the caller. | Assign the effective, real, and saved GIDs. The effective GID remains `u_groups[0]`. |
| `setegid(egid)` | Reject `NOGROUP` with `EINVAL`; otherwise the requested GID equals the real or saved GID, or `suser()` admits the caller. | Assign `u_groups[0]`. Preserve real and saved GIDs and every supplementary slot. |
| `setgroups(count, list)` | `suser()` admits the caller; `count` is at most `NGROUPS`; `copyin` succeeds; every staged entry differs from `NOGROUP`. | Copy the complete staged prefix, then fill every remaining slot with `NOGROUP`. |
| `getgroups(capacity, list)` | `capacity` covers the last occupied group slot and `copyout` succeeds. | Return the current `gid_t` vector and its count. Credentials remain unchanged. |
| `groupmember(gid)` | Scan from slot zero through the first `NOGROUP` or `NGROUPS`. | Return membership without changing credentials. |

Rejected transitions preserve the effective, real, and saved UIDs, the
process-table UID, the real and saved GIDs, and all `NGROUPS` group slots.
`setgroups()` copies into a fixed `NGROUPS` staging array and validates every
entry before publishing any group.  The gate injects `EFAULT` after writing
one `gid_t` into that private array; the published vector remains unchanged.
The publication contract describes transactional refusal followed by
non-failing assignments.  It makes no interrupt-level atomicity claim.

The process-group and identity-query behavior in `kern_prot.c` is retained.
`getpgrp()` reads no argument and returns the current process group.
`setpgrp()` rejects values outside `PGRP_VALID`, resolves PID zero to the
caller, and admits an owned process, a descendant, or a root caller under the
existing BSD rule.

## ABI and caller closure

The syscall master and generated dispatch table retain the existing ordinal
and argument-count records.  The RP2040 syscall boundary continues to carry
32-bit argument words.  `uid_t` and `gid_t` remain the existing public types;
the group handlers and host fixture now describe vectors as `gid_t *`.
`struct user`, `struct proc`, and every public layout remain unchanged.

The production `suser()` symbol replaces the test-local copies in both the
credential and signal-authorization fixtures.  The credential fixture links
`kern_prot.c`, `kern_prot2.c`, and `kern_proc.c`.  The ILP32 signal fixture
links `kern_sig.c`, `kern_prot2.c`, and `kern_proc.c`.  The filesystem explicit
timestamp fixture now links the `suser()` owner under function-section garbage
collection beside `ufs_fio.c`.  That dependency caught the stale fixture after
the ownership move.

`CRED_KERN_CFLAGS` adds `-Wstrict-prototypes` and
`-Wold-style-definition` to warnings-as-errors compilation for every
credential production object used by these fixtures.  The PICO and PICO_UART
production builds compile the same sources with their exact GNU17 Cortex-M0+
commands.  The generated kernel interface inventory contains no remaining
old-style definition for either credential translation unit.

## Calibrated behavior evidence

The native credential executable runs 333 assertions.  It covers identity
queries, process lookup, process-group admission, group reads and writes,
group membership, effective and permanent UID transitions, effective and
permanent GID transitions, wide IDs, the `NOGROUP` sentinel, copy failures,
and complete credential preservation after rejection.

The correctness runner requires fifteen independent source mutations to exit
with the fixture's assertion-failure status:

- invalid process-group width;
- `getpgrp()` returning the process ID;
- acceptance of `NOGROUP` inside a supplied group vector;
- non-root privilege admission;
- bypassed admission in `setuid`, `seteuid`, `setgid`, `setegid`, and
  `setgroups`;
- retained saved UID after `setuid`;
- stale process-table UID after `seteuid`;
- retained saved GID after `setgid`;
- real-GID publication by `setegid`;
- group publication before complete validation; and
- retention of the old group tail.

`check-kernel` also runs the credential executable under address and
undefined-behavior sanitizers, including alignment checks, when the native
compiler probe establishes that runtime capability.  The execution inventory
records an explicit skipped state when the capability is unavailable.
`check-kernel-ilp32` exercises the production privilege predicate through
signal authorization and the filesystem timestamp caller at target width.
The kernel metadata gate checks the unchanged syscall-dispatch declarations.
The C17 kernel inventory compares the lexical and compiler oracles for both
target configurations.

## Measured target resources

Clean distributions compare parent `4514cd81be7d82e03d5d97977398e3aa3b6d7921`
with implementation commit `34df4b4156deee5924aa0c221867c6bd933fa4ba`.
Both captures used bmake 20260912, arm-none-eabi-gcc 16.2.0, GNU ld
2.47.20260726, and Python 3.14.8. The schema-2 comparison reports zero
positive deltas and unchanged archive/member selections across both kernels,
every installed a.out, the native startup object and libc archive, and the
packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash bytes | 104,920 | 104,920 | 0 |
| PICO `.bss` / SwapRAM pool / scratch bytes | 15,272 / 16,384 / 8,192 | 15,272 / 16,384 / 8,192 | 0 / 0 / 0 |
| PICO_UART flash / `.bss` bytes | 92,428 / 14,832 | 92,428 / 14,832 | 0 / 0 |
| Three affected objects, text/data/BSS bytes per configuration | 1,444 / 0 / 0 | 1,444 / 0 / 0 | 0 / 0 / 0 |
| Native libc bytes / members | 43,010 / 151 | 43,010 / 151 | 0 / 0 |
| Packed-root data / free blocks | 692 / 287 | 692 / 287 | 0 / 0 |
| Packed-root free inodes | 33 | 33 | 0 |

The object comparison exposes the intentional ownership move without hiding it
inside the zero linked-kernel delta.  `kern_prot2.o` gains the 32-byte `suser`
symbol and `ufs_fio.o` loses the same 32 text bytes; `kern_prot.o` remains 556
text bytes.  Exact recorded PICO and PICO_UART commands replayed with
`-fstack-usage` report the same frame for every preserved function.  `suser()`
retains a zero-byte static frame, `setgroups()` remains the largest credential
frame at 72 bytes, and `ufs_setattr()` remains the largest measured
filesystem-object frame at 32 bytes.  A compiler-reported frame bounds one
function; it does not establish a complete call-chain or runtime high-water
mark.

The evidence hashes are:

| Receipt | SHA-256 |
| --- | --- |
| Parent source snapshot | `9635ad5e31a1720366599bc0089512664247082a59db4acdf3fc01ee0b1098d6` |
| Parent dependency closure | `7e59ba405c624b4df003892666145ce8c229a0c2e27f2dbc17e2d97020e370a4` |
| Parent schema-2 resources | `a6282db231aae6ccc96d5bab293290045fa5dcc605a16998d84deb05c2992e7a` |
| Candidate source snapshot | `f0c4975d135be709dfa8dba9d2d88ba2b34478078e7d3553f5f751fb4fffb47a` |
| Candidate dependency closure | `c352c3ca6a5fbd4f398b9e1838593b230d39aa3f73b5d774a11e900bd40a98ca` |
| Candidate schema-2 resources | `8f4f85cf53ba18689afb6c1dfc473e9df5092d875dee1f9fd785c8e77ef5a4a4` |
| Static-resource delta | `c9922e8f2a37fc99ecfca6d809d3d2b7309ad6d45e8df05705d099894e9a8a13` |
| Exact-command stack/object measurement | `307934121eaf66638bf63297540578af80e64c460f56154f0b8c9ecb8f4101bf` |

## Evidence states and residuals

The host fixtures establish the exercised admission, refusal-preservation,
publication, and sanitizer contracts.  The exact target builds establish
GNU17 Cortex-M0+ compilation and linkage.  These results do not execute a
credential transition on RP2040 silicon, exercise MPU privilege transitions,
or measure runtime stack and heap high-water marks.

The paired receipts accept the static kernel, affected-object, shipped a.out,
packed-root, native-artifact, and archive-selection surfaces for implementation
commit `34df4b4156deee5924aa0c221867c6bd933fa4ba`.  Runtime stack and heap
high-water marks remain unmeasured.  Credential transitions do not write
persistent storage, so this unit makes no flash-wear claim.

Negative `getgroups()` capacity, zero-length group-list semantics,
`setreuid()`, `setregid()`, and `initgroups()` policy remain separate migration
units.  The broad declarations in `sys/systm.h` and `sys/proc.h`, the remaining
filesystem operations in `ufs_fio.c`, and signal behavior outside the linked
authorization seam retain their own source-denominator states.  Physical-board
execution requires separate authorization for the exact image and workload.
