# Selected-entry scanner allocation and failure contracts

`scandir()` grows its pointer vector from accepted entries, publishes its
result only after the scan succeeds, and releases every owned allocation on
failure. The typed iterative heap sort carries comparator state through each
call and imports neither `qsort` nor compiler integer division. Compact
copies report their allocated `DIRSIZ` in `d_reclen`.

The source baseline is `e9d09a36b657b5b6f8ad55ab3476458eea5c9ffc`.
The scanner source hashes identify the measured implementation independently
of documentation edits:

| Source | SHA-256 |
| --- | --- |
| Baseline `lib/libc/gen/scandir.c` | `12be083bb61bb862dbaca3e17c7baba1b6976bd54174c2754bdc882d258e0a57` |
| Candidate `lib/libc/gen/scandir.c` | `3bf30a010abdcb6e7b2a2e3d4c6dc8a1436846fedb97607aa21c8e11120019f8` |

## Mechanism, interfaces and ownership

The inherited scanner derives initial pointer capacity from `st_size / 24`
and grows it from `st_size / 12`. Directory metadata describes disk records,
while a selection describes independent retained objects. A sparse selection
therefore inherits a capacity estimate unrelated to its result. Zero-sized
metadata also reaches `malloc(0)`, whose permitted outcomes provide an
unsuitable admission rule.

The candidate delays vector allocation until the first accepted entry and
uses capacities 4, 8, 16 and subsequent doubling, capped at `INT_MAX`.
The multiplication guard rejects an unrepresentable allocation size before
allocation. The count guard returns the distinct `EOVERFLOW=82` before a
count exceeds the public `int` result. Existing errno values retain their
numbers. Both public errno paths resolve to the same header; the master,
generated `kernel_errmsg()` metadata and calibrated metadata gate admit the
new error. The textbox-local `getline()` capacity guard retains its
`ENOMEM` diagnostic; its stale header-availability comment becomes a
description of that allocation bound. The SUS `getline()` overflow
diagnostic requires an independently tested interface change.

Each loop clears errno immediately before `readdir()`. A selector's errno
side effect cannot turn a later end-of-directory result into a read error.
Read and metadata failures retain their deciding errno across frees and
stream cleanup. Allocation-failure injection covers every request ordinal,
including vector movement and copied records. The failure path preserves the
caller's output pointer. Empty and all-rejected scans return zero with a
null, safely freeable output.

The selector accepts `const struct direct *`. The comparator accepts two
`const struct direct **` arguments. The adapter constructs two local const
entry pointers rather than casting an incompatible callback or an array
element's pointer type. Heap sorting bounds the iteration and retains all
comparator state in the call chain. A comparator that recursively scans a
second directory therefore preserves the outer ordering. Equal keys retain
both records with unspecified relative order.

The tree preserves its historical `<sys/dir.h>` and `struct direct` names.
`alphasort()` uses `strcmp()` in the fixed bytewise locale and preserves
errno. Locale-aware `strcoll()` and the standard `<dirent.h>` naming surface
remain separate compatibility work. The scanner unit establishes the
exercised allocation, failure and callback contracts; the unit supplies a
bounded Issue 7/Issue 8 improvement rather than edition-wide conformance.
`opendir()` directory admission/CLOEXEC and the `closedir()` result transition
remain separately owned roadmap units.

UUCP has an unrelated five-argument helper named `scandir`. The public
declaration exposes that source collision. `uusnap.c` names the private helper
`scan_spool_directory` and gives it an exact static signature; its spool
selection behavior retains the existing argument values.

## BSD comparison and independent adaptation

The comparison reads each scanner source and records each donor revision:

| Tree | Revision | Source |
| --- | --- | --- |
| 2.11BSD | `0a38e59d742ff4371b78ea65c5bcc50477b31708` | `lib/libc/gen/scandir.c` |
| 4.4BSD-Lite2 | `b10da34408c8a0f32f51d79a7f16bd75337115d2` | `lib/libc/gen/scandir.c` |
| OpenBSD | `8849c5d46b9b299f5d887033706f1ddd60f8f966` | `lib/libc/gen/scandir.c` |
| NetBSD | `1e8e727e461bb760bf82162f33b64afea02da2f3` | `lib/libc/gen/scandir.c` |
| FreeBSD | `88e7371d9dc26f85dfc1b008cbe59ebc7e4a33da` | `lib/libc/gen/scandir.c` |
| RetroBSD | `10e2a8d731502d7dfc4ca87ff87a586f194eafa6` | `src/libc/gen/scandir.c` |
| LiteBSD | `03934b78b7c78c2ecc52dd63d597c7847ebc1704` | `lib/libc/gen/scandir.c` |

The comparison runs `git rev-parse HEAD` and reads each named file. The
historical implementations supply the compact-record precedent. FreeBSD
supplies the useful separation of read-error detection, geometric capacity
and saved-errno cleanup. NetBSD supplies saved-errno cleanup but retains a
metadata-derived capacity estimate. OpenBSD retains a metadata estimate and
adds allocation bounds. The target adapts those mechanisms to its own
allocator, record layout and typed comparator without copying a donor's
larger directory or locale machinery.

All seven inspected scanners allocate compact records while retaining the
source record's block-relative `d_reclen`. A standalone copy has a different
ownership boundary: its allocation ends at `DIRSIZ`, rather than at the end
of a directory-block span. Normalizing the copied length makes the allocation
and its public extent agree. The source-length mutation fails the compact
length assertion, and the maximum-name case exercises the full record.

## Resource coefficients and measurements

Let `A` be the accepted count, `P` the pointer width, `R_i` each copied
record's `DIRSIZ`, and `C(A)` the next admitted geometric capacity. Retained
requested storage is `P * C(A) + sum(R_i)`, with zero for `A=0`.
At a moving resize from capacity `C` to `2C`, the transient request is
`3 * P * C + sum(R_i for i < C)`. The allocator shim counts both vector
allocations before releasing the old one. Those coefficients describe
requested storage; allocator headers, rounding, fragmentation, arena growth,
the real directory allocation and recursive callbacks require separate
accounts.

The paired probe selects nine names with sixteen-byte copied records and
1,024-byte metadata. The baseline capacity is `floor(1024 / 24)=42`.
The candidate capacity is 16. The candidate's final vector resize occurs
after eight copied records, so its peak is `24P + 128`, while retained
storage is `16P + 144`.

| Requested bytes, observed | Baseline ILP32 | Candidate ILP32 | Baseline native, P=8 | Candidate native, P=8 |
| --- | ---: | ---: | ---: | ---: |
| Retained | 312 | 208 | 480 | 272 |
| Moving-realloc peak | 312 | 224 | 480 | 320 |

The candidate contract gate asserts its exact retained and transient values.
The resource-probe mode executes the same production scanner and fixture
under either header set. The table describes the named workload; geometric
growth can cost more than an accurate metadata estimate for other counts.
With a four-slot vector and maximum 72-byte records, the first accepted
record requests 88 bytes at ILP32, excluding the directory and allocator.

Paired clean distribution builds use ARM GCC 16.2.0, host GCC 16.2.1,
Python 3.14.7 and bmake 20260912, GNU17 production flags and `-Os` userland.
Both `bmake -j4 MACHINE=rp2040 distribution` commands return zero.

| Artifact | Baseline | Candidate | Difference |
| --- | ---: | ---: | ---: |
| ELF and a.out `scandir.o` text/data/BSS | 276 / 0 / 0 | 508 / 0 / 0 | +232 text |
| Scanner static frame | 104 | 104 | 0 |
| `alphasort` static frame | 8 | 16 | +8 |
| Typed sift static frame | absent | 48 | +48 per call |
| Full ELF archive bytes / entries | 406372 / 380 | 406644 / 380 | +272 bytes |
| Full a.out archive bytes / entries | 115614 / 370 | 115814 / 370 | +200 bytes |
| Selected board archive bytes / entries | 56058 / 151 | 56058 / 151 | 0 |
| Installed packed-root archive bytes | 56058 | 56058 | 0 |
| PICO kernel text/data/BSS | 104610 / 248 / 39848 | 104646 / 248 / 39848 | +36 text |
| PICO_UART kernel text/data/BSS | 92184 / 192 / 14832 | 92220 / 192 / 14832 | +36 text |
| Root free 1,024-byte blocks | 265 | 265 | 0 |
| Off-manifest `uusnap` text/data/BSS | 13312 / 480 / 8748 | 13268 / 480 / 8748 | -44 text |

The candidate scanner imports `closedir`, `errno`, `free`, `fstat`, `malloc`,
`opendir`, `readdir`, `realloc` and `strcmp`. The baseline additionally
imports `qsort` and `__aeabi_idiv`. The baseline scanner's local text is
smaller, but a newly linked consumer also needs its generic sorting and
division closure. The measured candidate avoids those imports; a consumer
that already needs them has a different marginal cost.

The errno pool grows by 30 bytes and its uint16_t offset table grows by two.
Object alignment adds two bytes, so `errlist.o` grows from 2064 to 2098 text
bytes. Final link alignment produces the observed 36-byte kernel increase.
Writable kernel data and BSS retain their measured totals.

`boardlibc-members` excludes the scanner. Extraction compares all 150 object
members byte for byte; every object matches across the two selected archives.
The 151st archive entry is its symbol index. Archive header timestamps change
the complete archive hashes while file length and extracted objects match.

[libc-scandir-linked-extents.csv](libc-scandir-linked-extents.csv) records the
same 275 regular OMAGIC files in both complete install trees, with section
extents, lengths and hashes. Of those, 273 match byte for byte. `uusnap`
changes through the helper rename; `pdc` retains every extent and file length
but embeds its compilation time through `__TIME__` and `__DATE__` in `pdc.y`.
Both programs stay outside the selected manifest. The `fsutil` listings
report equal lengths for every packed program and archive. `/etc/release`
grows by six bytes for the candidate's `-dirty` provenance suffix, while
root free blocks retain the measured count. Complete image hashes also
include creation times and therefore supply a different comparison surface.

Static frames describe individual compiler frames. Callback recursion,
allocator stack, process/kernel high-water, board timing and flash traffic
remain unmeasured by these host and build observations.

## Gates, calibration and replay

Select and export the interpreter before each build or gate:

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-scandir-contracts
bmake MACHINE=rp2040 check-scandir-contracts-cross
bmake MACHINE=rp2040 check-kernel-metadata check-test-execution
bmake MACHINE=rp2040 check-lint
bmake -j4 MACHINE=rp2040 distribution
bmake -j4 MACHINE=rp2040 check
arm-none-eabi-size lib/libc/gen/scandir.o \
    sys/arch/rp2040/compile/PICO/unix.elf \
    sys/arch/rp2040/compile/PICO_UART/unix.elf
arm-none-eabi-nm -u lib/libc/gen/scandir.o
tools/bin/rp2040/size lib/libc_aout/libc/scandir.o
tools/bin/rp2040/ar t distrib/obj/boardlibc.rp2040/libc.a
tools/bin/rp2040/fsutil -v --partition=1 distrib/rp2040/sdcard.img
tests/dirent_contracts/scandir_contract_test --resource-probe
tests/dirent_contracts/scandir_contract_test32 --resource-probe
```

Use clean separate worktrees for a before/after consumer measurement.
`bmake build` leaves existing program links intact after a libc edit, so an
incremental consumer cannot establish a linked comparison. The CSV reads the
first eight little-endian 32-bit header words, admits `a_midmag & 0xffff ==
0407`, takes words 1 through 3 as text/data/BSS, and rejects a changed path
denominator before comparing hashes and lengths.

For the baseline requested-byte observation, set `SCANNER_ROOT` to the
baseline tree, `CONTRACT_ROOT` to the tree carrying the fixture, and
`SCANNER_PROBE_DIR` to an evidence directory. Repeat with `PROBE_ILP32=yes`
for four-byte pointers and with the candidate scanner root for the other
two columns. The explicit declarations apply only to the comparison lane;
the normal contract build consumes the public declarations.

```sh
: "${SCANNER_ROOT:?select the scanner source and matching headers}"
: "${CONTRACT_ROOT:?select the tree carrying the contract fixture}"
: "${SCANNER_PROBE_DIR:?select a retained evidence directory}"
mkdir -p "$SCANNER_PROBE_DIR"
set --
if [ "${PROBE_ILP32:-}" = yes ]; then set -- -m32; fi
cc "$@" -std=c17 -Wall -Wextra -Werror -fno-builtin -nostdinc \
    -isystem "$SCANNER_ROOT/include" \
    -Dscandir=discobsd_scandir -Dalphasort=discobsd_alphasort \
    -Dopendir=test_opendir -Dreaddir=test_readdir -Dclosedir=test_closedir \
    -Dfstat=test_fstat -Dmalloc=test_malloc -Drealloc=test_realloc \
    -Dfree=test_free -Derrno=dirent_test_errno \
    -c "$SCANNER_ROOT/lib/libc/gen/scandir.c" \
    -o "$SCANNER_PROBE_DIR/scanner.o"
cc "$@" -std=c17 -Wall -Wextra -Werror \
    -c "$CONTRACT_ROOT/tests/dirent_contracts/scandir_alloc_shim.c" \
    -o "$SCANNER_PROBE_DIR/allocator.o"
cc "$@" -std=c17 -Wall -Wextra -Werror -fno-builtin -nostdinc \
    -isystem "$SCANNER_ROOT/include" -DSCANDIR_RESOURCE_BASELINE \
    "$CONTRACT_ROOT/tests/dirent_contracts/scandir_contract_test.c" \
    "$SCANNER_PROBE_DIR/scanner.o" "$SCANNER_PROBE_DIR/allocator.o" \
    -o "$SCANNER_PROBE_DIR/allocation_probe"
"$SCANNER_PROBE_DIR/allocation_probe" --resource-probe
```

The host gate exercises empty/all-rejected lists, growth boundaries,
selector side effects, independent copies, maximum names, duplicate keys,
nested comparators, metadata and read failures, allocation failures, errno
restoration and transactional publication. Six mutations each fail a named
assertion. The cross import gate calibrates allowed symbols, all forbidden
helpers and a failed inspector before inspecting the real object. Optional
ILP32 and sanitizer capability probes emit execution or skip receipts.
Allocation multiplication and `INT_MAX` exhaustion remain source-reviewed
arithmetic bounds rather than billion-entry runtime fixtures. Host comparison
uses host `strcmp`; target compilation supplies the target import boundary.
Board measurements remain governed by roadmap item 12 and explicit opt-in.

The focused scanner, cross, metadata, execution-inventory and repository lint
gates return zero. The complete non-hardware `check` aggregate returns zero
after the paired clean distribution builds. The aggregate executes Smaller C
`t18_stdio` with the changed directory header and rejects the damaged
`filbuf.o` archive control. The fixture rerun supplies native compiler/linker
and qemu-user execution evidence; board loading and execution retain their
separate opt-in boundary.

Static-frame collection reads the evaluated `CC`, `CFLAGS` and `CPPFLAGS`
with `bmake MACHINE=rp2040 DEFS='-Wall -ffreestanding' -v CC -v CFLAGS
-v CPPFLAGS` in each revision's `lib/libc/gen` directory. A direct invocation
of that compiler and those flags adds `-fstack-usage`, compiles the revision's
`scandir.c` into a retained evidence directory, and reads the matching `.su`.
The collector compiles each source independently of existing build objects.
