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

The host driver links the complete database translation unit without
platform-specific dead-code elimination flags. The ARM witness alone requests
ELF section collection with its minimal startup support. Clang's
`-Wno-deprecated-non-prototype` applies only to the retained database source,
whose record operations still use C17-permitted old-style definitions; the
header and typed fixture retain their strict diagnostics. GCC, Clang, ILP32
and ARMv6-M QEMU runs pass the 3,180 cases and reject the eight controls.
Apple linker execution remains a hosted macOS qualification requirement.

The injected variadic `open` reads the creation mode using its default-promoted
integer type. C17 6.5.2.2 applies integer promotions to variadic arguments;
reading a narrow `mode_t` directly with `va_arg` has undefined behavior. Unary
plus and `_Generic` select the promoted type without compiler extensions.
Seven isolated type fixtures cover unsigned short, signed/unsigned int,
signed/unsigned long and signed/unsigned long long. GCC, Clang and ARMv6-M
QEMU pass these fixtures, while the direct narrow-type `va_arg` mutation fails
compilation with its promotion diagnostic. These fixtures establish type-width
portability independently of the host's actual `mode_t` definition.

The witness establishes public declarations and constructor behavior under
its injected allocation and I/O schedules. Database page parsing, hashing,
fetch/store/delete/iteration semantics, concurrent access, crash persistence,
runtime resource effects and physical-board behavior remain open.

## Static resource accounting

Matched clean schema-2 captures compare integration baseline
`01c8dbfde957fcfddfbd6b892d39af65265ab8f1` with merged candidate
`3a8b388df9ee2283a5bb37099dc468bc320ab515`. Both distributions use ARM GCC
16.2.0, GNU ld 2.47.20260726 and bmake 20260912. Their dependency closures
account for the same 37 artifact groups.

The comparison reports zero positive or negative deltas across all 158
constrained shipped numeric surfaces. All 33 program records, both kernel
resource vectors, both native artifacts and every archive selection remain
equal. The installed native libc remains 45,078 bytes with 159 members. The
root remains at 746 data blocks, 233 free blocks and 32 free inodes. Root and
kernel hashes differ because their release identities bind the distinct source
commits; their measured resource vectors and archive selections remain equal.

The unshipped full cross libc archive grows from 447,164 to 447,256 bytes.
Its `ndbm.o` member grows from 4,508 to 4,600 bytes, accounting for the exact
92-byte build-only delta. No shipped program or native archive selects that
member. The compatibility profile therefore requires no shipped-resource
exception for this candidate. Runtime stack, heap, storage traffic and physical
board timing remain unmeasured.

The candidate source, closure, resource and comparison receipt SHA-256 values
are `47f6b11f1a4705b393c63d97b759afed16ef07be997032a2510553f3079b8f77`,
`1a4102023e85dc8e2d49ec5d9acace18fa670ab2d1f776b516d66b496e01c41a`,
`a481fb3e6a6642e96aa1a9ae84bf74deedfa4080cf7907c848dbde933cbcba90`
and `76212dba83b169c5b28dd1caeca7f66b87e7d09088ed5c51c0dd04a331c5f627`.
The baseline source, closure and resource receipt SHA-256 values are
`7fb77b909e2464739df5ac121e19eb1e32f322379e3c26f45d56a1d19d2b2b33`,
`40c8a51d736c89ef54b63828982597cb6cc2a95b142263978a56e547bac8e476`
and `0d2c89db978bd125a9eb27d4208ddf4de753883f367100de48f49bd1e6aa1c36`.
