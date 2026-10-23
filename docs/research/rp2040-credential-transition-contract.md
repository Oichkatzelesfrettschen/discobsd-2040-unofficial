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

The native credential executable runs 267 assertions.  It covers identity
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

## Evidence states and residuals

The host fixtures establish the exercised admission, refusal-preservation,
publication, and sanitizer contracts.  The exact target builds establish
GNU17 Cortex-M0+ compilation and linkage.  These results do not execute a
credential transition on RP2040 silicon, exercise MPU privilege transitions,
or measure runtime stack and heap high-water marks.

Paired parent and candidate distribution receipts decide kernel flash, SRAM,
affected-object sections and static frames, every shipped a.out, packed-root
occupancy, and archive selection.  Those static resource states remain open
until the receipts bind an immutable implementation commit.  Credential
transitions do not write persistent storage, so this unit makes no flash-wear
claim.

Negative `getgroups()` capacity, zero-length group-list semantics,
`setreuid()`, `setregid()`, and `initgroups()` policy remain separate migration
units.  The broad declarations in `sys/systm.h` and `sys/proc.h`, the remaining
filesystem operations in `ufs_fio.c`, and signal behavior outside the linked
authorization seam retain their own source-denominator states.  Physical-board
execution requires separate authorization for the exact image and workload.
