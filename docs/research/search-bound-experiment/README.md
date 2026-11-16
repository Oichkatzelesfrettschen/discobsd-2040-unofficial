# Tableless Two-Way search experiment

The immutable FreeBSD donor is f3ec447a1c0697f024dcf12929552e4ee9a420e0,
lib/libc/string/memmem.c. The source hash is
cb0fd325441b8268d6aca15020c7561db019fb13bb5fc9d7022ac853b067d02c.
The target baseline is e0db6c50379f6537e9c5af5e03520282a55cbe51,
lib/libc/string/memmem.c. The retained experiment supplies no shipped replacement.

The adaptation removes the 256-entry last-byte shift table, its 32-byte
presence set and all short-needle accelerators. Maximal-suffix preprocessing,
period selection, right-then-left matching, remembered matched prefix and shifts
retain the donor mechanism. A typed inline loop replaces periodic memcmp.
The sole donor compilation adaptation casts a nonnegative pointer difference
to size_t to satisfy signed-comparison diagnostics. The MIT notice stays intact.
Unsigned SIZE_MAX denotes suffix index -1; adding one normalizes the critical
split to a size_t count. The experiment adds no dynamic allocation or global
production state. COUNT_COMPARISONS adds a test-only counter.

GCC native, Clang ILP32 and Clang address/undefined sanitizer runs each pass
4,239,266 earliest-match and finite comparison-bound cases. The matrix includes
all binary haystacks through length 12 crossed with all binary needles through
length 8, 20,000 seeded byte trials, 512 structured periodic/near-periodic
trials and 33,153 protected-page boundary cases. Empty inputs, embedded NULs,
needle longer than haystack and high bytes remain in the input domain.
The independent oracle scans candidate offsets in increasing order and compares
bytes directly. The counted candidate must use at most 4n + 8m byte-order
operations and must record a comparison for every admitted nonempty input.
Those finite checks establish neither a general complexity proof nor cycles.
The fixtures' maximum observed candidate count is 5,132.

Three controls reject: accepting a completed right half without the left-half
check returns a false match; returning at the first mismatching candidate loses
a later match; disabling the counter reaches its specific zero-count
rejection. Expected exit codes are 1, 1 and 3. Compiler errors and unrelated
runtime errors never count as calibration. Guard pages expose reads beyond
the exact admitted haystack or needle extent in the exercised cases.

The ARMv6-M QEMU run passes the 4,206,113 non-guard cases and rejects the same
three controls. The fixture links newlib and the existing Linux-EABI QEMU
support; it establishes instruction execution under that support, independently
of DiscoBSD process admission or physical-board behavior.

The initial skip-candidate control increments a shift beyond its required value.
Native and ILP32 execution reach the wrong-result oracle, while sanitizer
execution detects an out-of-bounds read first. The harness rejects the latter
as a different failure. The retained early-return control preserves valid reads
and reaches the named result assertion in all four execution configurations.

ARM GCC 16.2.0, strict C17, -Os, -mcpu=cortex-m0plus, -mthumb,
-mfloat-abi=soft, -fno-builtin and -fstack-usage report:

| Source | Text bytes | Data/BSS | Compiler local frame |
| --- | ---: | ---: | ---: |
| DiscoBSD scan | 74 | 0/0 | 24 |
| Tableless experiment | 304 | 0/0 | 40 |
| FreeBSD donor with warning fix | 700 | 0/0 | 24 entry plus 1,104 core |

The experiment imports no external symbol. The baseline imports memchr/memcmp;
the donor additionally imports memset. Object-only comparisons exclude those
providers, final link placement, packing and complete caller stack occupancy.
The 1,056-byte donor table removal does not equal a shipped RAM saving because
DiscoBSD has no such table. Relative to DiscoBSD, the measured local costs are
230 text bytes and 16 entry-frame bytes. The entire process remains subject to
the fixed 192 KiB window and measured resource admission.

A separate counted baseline replaces memcmp/memchr with scalar byte comparators
and retains the target memmem source. For all-zero haystacks and a needle of
m-1 zero bytes followed by one, every candidate matches its first byte and
memcmp compares m bytes. The baseline counts below omit the separate first-byte
filter; they therefore undercount total baseline byte comparisons. The candidate
counts include preprocessing and period checks as well as search. These are
algorithm-operation counts, not matched instruction counts or speedups.

| Haystack n | Needle m | Baseline memcmp byte comparisons | Candidate byte-order operations |
| ---: | ---: | ---: | ---: |
| 256 | 16 | 3856 | 288 |
| 512 | 32 | 15392 | 576 |
| 1024 | 64 | 61504 | 1152 |
| 2048 | 128 | 245888 | 2304 |
| 4096 | 256 | 983296 | 4608 |
| 8192 | 512 | 3932672 | 9216 |

The baseline formula is (n-m+1)m for this family; when m=n/16 the leading
coefficient is 15/256 times n squared. The candidate measurements equal 9n/8
at these six points. The latter finite equality supplies no universal exact
cost formula. Crochemore-Perrin Two-Way and removal of optional skip tables
have established prior art. The target-specific finding is the measured
implementation/resource tradeoff under byte-object and process-window rules.

## Logical state and compiled frames

A separate variant replaces remembered-prefix reuse with repeated comparisons.
It removes `mem0` and assigns zero to `mem` after every shift, preserving the
donor's period and candidate shifts. The complete native matrix still passes.
The ARM object shrinks from 304 to 290 text bytes while the frame grows from
40 to 48 bytes. Disassembly shows the same 20-byte register push and an increase
from 20 to 28 bytes of local stack space. Logical variable count therefore
supplies no bound on the compiler's register allocation and stack requirements.
This variant establishes no resource improvement over the retained experiment;
its general comparison-bound argument remains open.

The experiment remains unshipped. General complexity justification, periodic
prefix cost controls, realistic workload timing,
strstr's NUL-boundary integration, caller closure and matched whole-distribution
resources remain deciding obligations. No physical-board operation executes.

Native compilation has its own selection boundary. At `11a3f970`,
`lib/libc/string/Makefile` selects `memmem.o` for the cross library, while
`lib/libc_aout/libc/Makefile` and `distrib/rp2040/boardlibc-members` omit it.
The reduced archive also omits `memchr.o`, a dependency of the existing scan;
the full converted archive selects that provider. The earlier
[array/memory/line promotion](../libc-array-memory-line-reading.md) explicitly
retains the native subset unchanged. A native search capability therefore needs
separate declaration, archive, dependency and resource admission. Tableless
object measurements supply no substitute for that closure. The source query is
`rg -n 'memmem|memchr' lib/libc/string/Makefile lib/libc_aout/libc/Makefile
distrib/rp2040/boardlibc-members distrib/rp2040/mkboardlibc.py`.

Artifact hashes:

- tableless.c: 022bc2d0e5b07955deee752cb248f12879eea6496e30580dd5ebb545d0e3e016
- fixture.c: 0fd26bbc90a53a394d2abe1f6b1659da19ef5e1067b834a9dec39acc6434a2e4

Source inspection uses `git show` at the full donor revision and `rg -n` on the
local `memmem` implementation. Artifact inspection uses `arm-none-eabi-size`,
`arm-none-eabi-nm -u`, `arm-none-eabi-objdump -dr` and compiler `.su` records.

## Replay and evidence boundary

`run.py` creates a fresh, label-specific directory under `.build-logs`, records
source hashes, compiler versions and each subprocess result, and writes a final
receipt. A positive execution requires exit zero, empty diagnostics and the
complete matrix marker. Each negative control requires its exact exit status
and named oracle diagnostic. A failed compile, timeout, unrelated crash or
missing result fails the run. Unselected execution variants explicitly record
`not run`; the receipt supplies no evidence for those variants. The experiment
is optional research and belongs outside the required execution inventory.

The retained executions use GCC 16.2.1, Clang 23.1.4, ARM GCC 16.2.0 and
QEMU 11.1.3 user-mode execution. Replay requires a C17 host compiler, POSIX anonymous
mapping support, and Python 3.10 or newer for the driver. ILP32 requires a
working 32-bit compiler/runtime; sanitizers require the selected Clang runtime.
ARM execution requires `arm-none-eabi-gcc` with newlib and `qemu-arm` supporting
`cortex-m0`. Compiler `--version` outputs and the QEMU version remain in the
generated evidence. A missing selected tool fails instead of skipping.

```sh
set -eu
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
"${PYTHON}" docs/research/search-bound-experiment/run.py \
    --label search-comparison --cc cc --ilp32-cc clang \
    --sanitizer-cc clang --arm-cc arm-none-eabi-gcc
```

`work_counts.c` supplies the separate operation-count fixture. Compile the target
`lib/libc/string/memmem.c` with `-Dmemmem=baseline_memmem`,
`-Dmemcmp=baseline_memcmp`, `-Dmemchr=baseline_memchr` and `-fno-builtin`;
link that object with `work_counts.c` and `tableless.c`, enabling
`-DCOUNT_COMPARISONS`. The resulting columns are haystack length, needle length,
baseline helper comparisons and candidate byte-order operations. Repeating the
fixture on another implementation requires retaining the same input families
and instrumentation definitions.
