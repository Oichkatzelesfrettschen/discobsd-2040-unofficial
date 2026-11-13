# Database pathname and open-state admission

At `fa93083fac449286382d45616c9d9549cc9e378f`, `dbm_open` copies an
unbounded basename into the 1,024-byte `dbm_pagbuf`, then appends `.pag` or
`.dir`. A host ASan/UBSan probe supplies a 4,095-byte pathname and read-only
flags. ASan reports a heap-buffer overflow at `lib/libc/gen/ndbm.c:111`
before either database file opens. The source SHA-256 is
`3b9a29341d7a6cd9fe40d331678073dfc19fa483baf83af12e6cfb6a01abe7cb`.

The governing invariant admits a basename only when its bytes, four-byte
suffix and terminating NUL fit the page buffer. Thus the maximum basename
length is 1,019 bytes. The same bound governs both suffixes. Rejection precedes
allocation and file operations and reports `ENAMETOOLONG`.

The open-state audit also finds unchecked `fstat` and signed multiplication
in the directory bit-index limit. A successful return requires two owned
descriptors and an admitted directory extent. Every failed stage releases
only acquired resources and preserves the stage's errno across cleanup.
For byte extent n, the maximum bit index is 8n - 1. For positive n, computing
8(n - 1) + 7 avoids an overflowing intermediate at the largest representable
index; admission requires n <= LONG_MAX / 8 + 1. Empty directories retain -1.

The implementation hypothesis predicts that an overlong basename returns NULL
with `ENAMETOOLONG` without allocation or I/O, every failed acquisition leaves
zero owned descriptors and allocations, and the largest admitted extent yields
`LONG_MAX` without UBSan diagnostics. A sanitizer error, truncated filename,
unexpected file operation, leaked resource or changed failure errno falsifies
the hypothesis. Tests inject allocation, both open stages and fstat failures,
exercise both sides of the pathname and extent boundaries, and reject mutations
of admission and cleanup. The full database record parser retains a separate
unreviewed contract.

## Public contract and caller closure

`include/ndbm.h` includes `sys/types.h`, guards repeated inclusion and declares
every public entry with a complete prototype. `dbm_open` has the Issue 8 type
`DBM *dbm_open(const char *, int, mode_t)`. Both host-table callers pass mode
zero with `O_RDONLY`; `mkpasswd` retains its creation mode. A two-argument
caller now fails compilation instead of depending on an unprototyped call and
an indeterminate third argument.

The constructor initializes both descriptor cells to -1 before acquisition.
Each failure path closes only a nonnegative descriptor, frees the database and
restores the errno produced by the failing stage. A successful return owns both
descriptors and records -1 for the empty directory's maximum bit index.

## Donor mechanisms and selection

| Donor revision | Mechanism | Local decision |
| --- | --- | --- |
| DragonFlyBSD `805ca132aededa6ae4c8b3499a2d5c934d78fe56` | `include/ndbm.h` supplies the exact `mode_t` prototype; `lib/libc/db/hash/ndbm.c` rejects an overlong basename before concatenation. Both files remain byte-identical at inspected development revision `94448f622a06cde90ba42d688e9cab2448ad9ab3` | Retain the signature and fail-before-I/O invariant; adapt it to the local two-file representation |
| FreeBSD `88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da` | The hash-backed constructor applies the same explicit length rejection | Count the shared Berkeley lineage once; use it as a compatible mechanism check |
| OpenBSD `8849c5d46b9b299f5d887033706f1ddd60f8f966` | `snprintf` publishes a length result and rejects negative or oversized output | Preserve the bound while avoiding formatter linkage in the tiny local constructor |
| NetBSD `1e8e727e461bb760bf82162f33b64afea02da2f3` | `strncpy` plus `strncat` can truncate the suffix and leaves a source-level termination hazard at the copy bound | Reject this mechanism; a database name never aliases a truncated pathname |
| RetroBSD `10e2a8d731502d7dfc4ca87ff87a586f194eafa6` | The inherited two-file implementation uses unchecked `strcpy` and `strcat` and declares an `int` mode | Preserve its compact representation and replace its unsafe admission and untyped call boundary |

The four modern BSD headers share Berkeley ancestry, so their agreement does
not supply four independent confirmations. The local composition keeps the
historical `.pag` and `.dir` storage while importing only the modern public
type and fail-closed pathname invariant.

## Arithmetic and executable witnesses

The pathname bound follows directly from the object extent:

```text
maximum basename = 1024 - 4-byte suffix - 1-byte terminator = 1019
```

The directory extent n publishes the maximum bit index

```text
n = 0: -1
n > 0: 8 * (n - 1) + 7
```

Admission requires `n <= LONG_MAX / 8 + 1`, so every intermediate and result
fits `long`. The equivalent expression `8n - 1` states the result but can
overflow before subtraction.

`check-libc-dbm-open` runs 3,075 pathname/access combinations and 105 staged
failure/extent combinations, for 3,180 cases at native and available ILP32
widths. `check-qemu` repeats the same source and eight controls as ARMv6-M.
The controls remove the header guard, weaken the basename and extent bounds,
ignore `fstat`, corrupt cleanup errno, leak descriptor zero, omit the last bit
and select the wrong suffix. The accepted source passes each positive case and
every control fails at its named observation.

The witness establishes public declarations and constructor behavior under
its injected allocation and I/O schedules. Database page parsing, hashing,
fetch/store/delete/iteration semantics, concurrent access, crash persistence,
target resource effects and physical-board behavior remain open.
