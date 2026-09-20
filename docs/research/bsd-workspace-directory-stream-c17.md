# BSD directory-stream mechanisms composed for the RP2040

## Finding

The BSD workspace contains three directory-cookie designs, but none fits this
port unchanged. DiscoBSD's fixed 1 KiB buffer already gives the smallest
representation. OpenBSD and FreeBSD contribute the useful mechanism: a seek to
a location still inside that buffer changes an index instead of issuing a
system call and re-reading the block. The local `DIR` supplies the missing RAM
opportunity: its `dd_cur` entry occupies 72 target bytes and has zero readers.

The adopted representation replaces `dd_cur` with a four-byte `dd_seek` buffer
base. The resulting stream is 1,040 bytes rather than 1,108 bytes, recovers 68
heap bytes per open directory, makes `telldir()` arithmetic-only, and preserves
the fixed buffer. The five directory-stream members use C17 definitions and
full public prototypes.

## Sources and fit

The comparison read these live source identities on 2026-09-20:

| Tree | Commit | Mechanism considered |
| --- | --- | --- |
| 4.4BSD-Lite2 | `b10da34408c8a0f32f51d79a7f16bd75337115d2` | heap-backed opaque cookies and a separate rewind location |
| NetBSD 11 | `1e8e727e461bb760bf82162f33b64afea02da2f3` | per-stream heap-backed cookies and locking |
| OpenBSD | `8849c5d46b9b299f5d887033706f1ddd60f8f966` | search the resident buffer before seeking |
| FreeBSD | `88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da` | packed cookies and same-buffer index changes |
| 2.11BSD | `0a38e59d742ff4371b78ea65c5bcc50477b31708` | the same `lseek()`-derived cookie inherited here |
| DiscoBSD | `dd63c0c28d7ce4a0b1809dd802d1b2bb4eb39422` | fixed buffer, allocation-free absolute cookies, unused cached entry |

The 4.4BSD-Lite2 and NetBSD designs allocate cookie records and therefore add
failure, lifetime and heap-growth states to an interface whose complete local
position fits in one `long`. FreeBSD's packed-cookie fallback solves offsets
larger than the cookie bit fields; the RP2040 root is 1,536 KiB and the local
absolute offset already fits without packing. OpenBSD's buffer reuse transfers
without its thread and `getdents()` machinery.

LiteBSD and LiteBSD-2 carry the 4.4BSD stream family. RetroBSD and the local
2.11BSD checkout carry the ancestral fixed-buffer family. Their unchanged
implementations establish lineage rather than an independent mechanism.

## Representation and behavior

The stream maintains three values:

```text
dd_seek = absolute file offset represented by dd_buf[0]
dd_loc  = byte offset of the next entry inside dd_buf
dd_size = bytes in dd_buf supplied by the last successful read
```

`telldir()` returns `dd_seek + dd_loc`. `readdir()` advances `dd_seek` by the
previous `dd_size` only when the buffer is exhausted. `seekdir()` changes
`dd_loc` directly for every nonzero cookie in the inclusive resident interval.
Cookie zero backs `rewinddir()`, so it seeks and invalidates the resident bytes
to make first-block directory mutations visible. The inclusive upper bound
matters: a cookie at the block end parks at
`dd_loc == dd_size`, and the next `readdir()` advances to the next block
without a seek.

A seek outside the resident interval commits new stream state only after
`lseek()` succeeds. A failed seek therefore preserves the buffer, current
cookie and next entry. `opendir()` initializes every position field and
preserves the allocation error across descriptor cleanup.

`readdir()` validates the bytes before advancing. A record must have a complete
fixed header, a name length at most `MAXNAMLEN`, a record length at least
`DIRSIZ(entry)`, four-byte alignment, a terminating null at `d_namlen`, and an
extent inside the bytes returned by `read()`. A malformed record reports
`EIO`; end of directory retains the ordinary null result.

## Target accounting

The baseline objects were compiled from `dd63c0c2` with the evaluated RP2040
GNU17, Cortex-M0+, soft-float, `-Os`, freestanding header and warning flags.
The adopted objects use the same target flags plus the gate's explicit C17
old-style-definition rejection.

| Object | Baseline text | Adopted text | Delta |
| --- | ---: | ---: | ---: |
| `opendir.o` | 48 | 60 | +12 |
| `readdir.o` | 84 | 124 | +40 |
| `closedir.o` | 18 | 18 | 0 |
| `seekdir.o` | 52 | 96 | +44 |
| `telldir.o` | 24 | 8 | -16 |
| Five-member total | 226 | 306 | +80 |

The text increase buys record-bound checks, failed-seek atomicity and allocation
errno preservation. The resident stream allocation drops by 68 bytes. Runtime
work also drops on every tell and every nonzero seek whose cookie remains
resident.
Cycle and energy changes are inferred from the removed system calls; the gate
does not measure them.

## Verification and limits

`check-dirent-contracts` compiles the exact five members over deterministic
host shims. It covers initialization, valid and free records, both seek paths,
mutation-visible rewind, the block-end cookie, seek and read failures, six
malformed-record classes, allocation cleanup and close ownership. The
pre-change source fails its strict C17 compilation at the K&R definitions.

`check-dirent-contracts-cross` compiles the same members for Cortex-M0+, pins
`sizeof(DIR) == 1040`, pins the complete 1 KiB buffer, and rejects an undefined
`lseek` symbol in `telldir.o`.

These gates prove modeled behavior, target compilation, layout and symbol
closure. They do not prove filesystem behavior on the board. A board run that
enumerates a directory across two blocks, saves cookies on both sides of the
boundary and replays them would supply that evidence class.

Two adjacent ideas remain separate units. Changing `closedir()` from `void` to
the POSIX error-returning interface changes a public ABI and needs a consumer
sweep. Making `opendir()` reject a regular file immediately adds an `fstat()`
dependency and needs complete linked-image accounting before the correctness
gain can be admitted on this root budget.
