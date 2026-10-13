# Stream-owned unbuffered input

The stdio core stores an unbuffered input byte in the existing `FILE._ub`
slot. The 24-byte ARM `FILE` layout stays fixed. The change removes a
one-byte write beyond the static descriptor buffer and prevents two streams
sharing a descriptor from overwriting each other's pushed-back byte.

The source baseline is `8711f637e9cdefd7f1aa3628bd6262892673ebc0`.
Measurements use the default RP2040 distribution profile, GNU17 production
flags, `-Os` userland and the existing Pico SDK/boot-ROM interfaces. The
implementation changes libc storage ownership; kernel code and board
configuration remain outside the edit surface. The parent continuation plan
is [c17-semantic-migration-plan.md](c17-semantic-migration-plan.md).

## Mechanism and falsifier

`findiop.c` provides eight static `FILE` objects. Its former `sbuf[8]` has
one byte per static slot, but `_filbuf` indexes that array by `fileno(iop)`.
Descriptor allocation and stream allocation are independent: `fdopen(9,
"r+")` can occupy a free static stream while the side array still has eight
bytes. `_f_morefiles` expands the array only after every static stream is
active, so a bounds check based on the number of streams cannot repair the
ownership model.

The baseline ILP32 test duplicates a temporary-file descriptor with
`F_DUPFD` above `FOPEN_MAX`, wraps that descriptor, selects `_IONBF`, and
calls `getc`. AddressSanitizer reports a one-byte global-buffer-overflow
one byte beyond `sbuf`. The unsanitized ILP32 fixture exits on a memory
fault. The native-width fixture follows the old null-side-buffer path and
fails five pushback assertions.

A second counterexample stays within every descriptor bound. Two unbuffered
streams share a descriptor. The first reads `A` and pushes back `Z`; the
second reads `B`. Descriptor-owned storage makes the first stream return
`B` instead of `Z`. The committed `ansi_shared_byte_bad.c` control restores
that ownership while sizing the array for all legal descriptors. The gate
requires the independent-pushback assertion to reject the control. The
control therefore tests ownership without depending on an accidental crash.

The governing interfaces are C17 7.21.7.10 and
[Issue 8 ungetc](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ungetc.html):
pushback belongs to the stream. Issue 8 XSH
[2.5.1, Interaction of File Descriptors and Standard I/O Streams](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_05_01)
permits switching from an unbuffered stream without a flushing action.

## Lifetime proof and BSD comparison

`_filbuf` returns from `_IOSTRG` processing before descriptor refill. A
string stream therefore uses `_ub` for its separate pushback slot, while an
unbuffered file stream uses the same byte as its ordinary writable buffer.
Those lifetimes exclude each other. After a one-byte read, `_cnt` is zero
and `_ptr` points one past `_base`; `ungetc` writes or steps back within the
single-byte object. `_IOMYBUF` stays clear, so `fclose`, `freopen` and
`setvbuf` preserve the enclosing `FILE` storage when releasing buffers.

The address of an active `FILE` remains significant, as C17 7.21.3 permits.
The maintained stdio wrappers construct local string streams or mutate a
supplied stream pointer; the source search finds zero active FILE-by-value
copies. `fprintf`, `vfprintf` and `fputs` temporarily replace buffer pointers
on the original stream. Before returning, the wrappers restore `_IONBF` and
clear `_base`, `_bufsiz` and `_cnt`; `_ptr` retains the expired scratch-buffer
address. The zero count forces the next input through `_filbuf`, which installs
`_base = &iop->_ub` and rewrites `_ptr` before reading through the pointer.

386BSD commit `9eaf3b3359255f461055b8f582131dd4695e750d` provides the
small-system precedent: `usr/src/lib/libc/stdio/makebuf.c`, `__smakebuf`,
selects an inline `_nbuf[1]` for explicit unbuffered mode and allocation
failure. Its file SHA-256 is
`27c9b5f5f7206354216b5eb24a9fa62524ab20664eab96a9615087af262f2f10`.
The donor's `usr/src/include/stdio.h` carries separate pushback and input
arrays. DiscoBSD reuses its existing byte because its stream modes establish
disjoint lifetimes. The adaptation copies the ownership concept rather than
the larger donor layout or implementation.

## Resource derivation

Let `S` be the static stream count, `D` the descriptor limit, `P` the pointer
width, and `W` the allocator word size. The removed static storage is
`S * 1 + P`. Here `S=8` and `P=4`, so the object loses 12 mutable bytes.
Expansion removes one `calloc(D, 1)` request, while retaining the `D * P`
glue table and each dynamic 24-byte `FILE`. With `D=30` and `W=4`, the
allocator's rounded chunk formula gives `W * ceil((D + W) / W) = 36` bytes.
That arithmetic describes a removed chunk, not a measured 36-byte decrease
in the process break: malloc obtains arena space in larger units.

Linkage supplies another coefficient. A consumer of `findiop.o` alone saves
32 text + 4 data + 8 BSS = 44 process-window bytes. A consumer that also
links `filbuf.o` saves another 48 text bytes, totaling 92. Link alignment
explains small deviations in several final executables; the CSV records
those outputs rather than rounding them into the common result.

| Artifact | Baseline | Stream-owned byte | Measured difference |
| --- | ---: | ---: | ---: |
| `findiop.o` text/data/BSS | 356 / 196 / 16 | 324 / 192 / 8 | -44 window bytes |
| `filbuf.o` text/data/BSS | 324 / 0 / 0 | 276 / 0 / 0 | -48 window bytes |
| `_filbuf` static stack frame | 80 | 72 | -8 bytes |
| `/bin/box` text/data/BSS | 33548 / 1400 / 6812 | 33468 / 1396 / 6804 | -92 window bytes |
| `/sbin/adminbox` text/data/BSS | 25696 / 1720 / 2540 | 25616 / 1716 / 2532 | -92 window bytes |
| `/usr/bin/login` text/data/BSS | 21912 / 693 / 3435 | 21832 / 689 / 3427 | -92 window bytes |
| Packed-root `/usr/lib/libc.a` file | 56134 | 55882 | -252 bytes; 151 archive entries in both |
| Packed `/bin/box` file | 28536 | 28443 | -93 bytes |
| Packed `/bin/sh` file | 30819 | 30742 | -77 bytes |
| Packed `/sbin/adminbox` file | 22149 | 22074 | -75 bytes |
| Packed `/usr/bin/login` file | 19255 | 19164 | -91 bytes |
| Root free blocks | 265 | 265 | equal 1024-byte block count |
| PICO kernel text/data/BSS from GNU size | 104610 / 248 / 39848 | 104610 / 248 / 39848 | equal section-size totals |

[stdio-unbuffered-linked-extents.csv](stdio-unbuffered-linked-extents.csv)
records all 275 regular OMAGIC executable files in the complete installed
build tree, excluding symlinks. Of those, 263 change: 172 lose 92 window
bytes, 85 lose 44, three lose 96, two lose 48 and one loses 40. Twelve retain
their extents. Their summed file lengths fall by 17,868 bytes and summed
text+data+BSS by 19,988 bytes. The complete build tree exceeds the selected
root manifest; neither sum describes flash saved by the shipping image or
simultaneous resident memory.

Packed bytes and allocated filesystem blocks are different accounts. Every
listed packed consumer shrinks, while the filesystem still reports 265 free
blocks. Kernel text comparison finds seven differing banner bytes from build
number and time; the kernel section totals above remain equal. The evidence
omits builder identity. PICO_UART receives the aggregate's build/verification
coverage; a paired before/after PICO_UART artifact is unmeasured.

## Reproduction and acceptance

The measurement host uses ARM GCC 16.2.0, host GCC 16.2.1, Clang 23.1.1,
Python 3.14.7 and bmake 20260912. Select and export `PYTHON` before the
commands. Use separate baseline and candidate worktrees, the same toolchain
and profile, and retain baseline artifacts before rebuilding.

```sh
: "${PYTHON:?select the interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-lint
bmake -j4 MACHINE=rp2040 distribution
# After a libc edit, every consumer needs a clean relink.
bmake -j4 MACHINE=rp2040 clean
bmake -j4 MACHINE=rp2040 distribution
bmake MACHINE=rp2040 check-libc-ansi check-libc-rwmode check-libc-scanf
bmake -j4 MACHINE=rp2040 check
tools/bin/rp2040/size lib/libc_aout/libc/findiop.o \
    lib/libc_aout/libc/filbuf.o sbin/box/box sbin/adminbox/adminbox
arm-none-eabi-size sys/arch/rp2040/compile/PICO/unix.elf
tools/bin/rp2040/fsutil -v --partition=1 distrib/rp2040/sdcard.img
```

For the frame comparison, compile each revision's two stdio sources with
the evaluated production flags plus `-fstack-usage`. Read `.su` frame sizes;
the compiler output establishes static frames, not workload stack high-water.
The CSV decodes the first eight little-endian 32-bit a.out header words,
selects `a_midmag & 0xffff == 0407`, and reports words 1 through 3 as
text/data/BSS. Its file delta uses complete file lengths.

The expanded ANSI gate passes 125 native-width checks and 129 ILP32 checks.
The ILP32 AddressSanitizer/UndefinedBehaviorSanitizer variant passes the same
129 checks, and the descriptor-byte mutation fails its designated assertion.
The same ILP32 fixture also passes 129 checks under Clang with strict C17
and warnings as errors. The existing update-stream and scanner gates pass. The clean distribution
and full non-hardware `check` aggregate return zero. TESTING.md and
`tools/test-execution-inventory.json` own CI routing and conditional-runtime
receipts. Compiler capability probes keep unsupported variants explicit.

Language and interface acceptance cover the changed portable libc members
and exercised contracts. Resource acceptance covers object/linked extents,
static frame estimates, the packed-root archive and filesystem accounting.
Board input timing, runtime heap/stack high-water, flash traffic, wear and a
new native Smaller C stdio consumer remain unmeasured. The unchanged public
layout introduces zero new native-header language constructs.

### Replay the installed-executable ledger

Set `BASELINE_DESTDIR` and `CANDIDATE_DESTDIR` to the two retained
`distrib/obj/destdir.rp2040` directories. The collector rejects a changed
executable denominator and writes CSV to stdout. Use its output for
`stdio-unbuffered-linked-extents.csv`.

```sh
: "${PYTHON:?select the interpreter}"
: "${BASELINE_DESTDIR:?select the retained baseline install directory}"
: "${CANDIDATE_DESTDIR:?select the candidate install directory}"
"$PYTHON" - "$BASELINE_DESTDIR" "$CANDIDATE_DESTDIR" <<'COLLECT'
import csv
from pathlib import Path
import struct
import sys


def extents(directory):
    result = {}
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as source:
            header = source.read(32)
        if len(header) != 32:
            continue
        words = struct.unpack("<8I", header)
        if words[0] & 0xffff == 0o407:
            result[str(path.relative_to(directory))] = (
                *words[1:4], path.stat().st_size
            )
    if not result:
        raise SystemExit("empty executable inventory")
    return result


before = extents(Path(sys.argv[1]))
after = extents(Path(sys.argv[2]))
if before.keys() != after.keys():
    raise SystemExit("executable inventories differ")
writer = csv.writer(sys.stdout)
writer.writerow([
    "program", "before_text", "before_data", "before_bss",
    "after_text", "after_data", "after_bss", "window_delta", "file_delta"
])
for name in before:
    old, new = before[name], after[name]
    writer.writerow([
        name, *old[:3], *new[:3],
        sum(new[:3]) - sum(old[:3]), new[3] - old[3]
    ])
COLLECT
```
