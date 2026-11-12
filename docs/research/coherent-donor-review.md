# Coherent mechanisms for the DiscoBSD C17/POSIX profile

The supplemental Git donor is [gspu/Coherent](https://github.com/gspu/Coherent),
branch `master`, pinned at `a00b86ea22c66625cf1838a71c06129f5eda2b49`.
A shallow clone captures the development tip; full Git history remains outside
the observation. The commit contains 29,492 tracked paths, including 4,423 `.c`
and 2,375 `.h` paths. Those counts describe names rather than maintained build
closure or independent implementations. The Git-ordered NUL-delimited path list
hashes to `fcbeb535ff81abe858aa8095dfeaf55f673ba7dfecdd1739874f7d2221030dfa`.

Coherent supplements the 20-repository BSD workspace and 13-root archive
frontier. DragonFlyBSD remains the preferred current implementation reference.
Standards and independent DiscoBSD witnesses decide adaptation. The review
reads source and Git metadata; historical tools and binaries remain unexecuted.
The donor gate validates the BSD/archive ledgers. The supplemental identities
below have a separate manual Git-object replay rather than coverage by that gate.

## Source selection and licensing

`mwc/romana/source/4.2.x/usr/src/misc/regexp.c` is byte-identical to
`mwc/romana/relic/g/usr/lib/misc/regexp.c`; the relic/e variant differs.
The inspectable libc source/build pair resides under
`mwc/romana/relic/b/lib/libc`. XSTDIO is a distinct variant. RCS `,v` files
are revision databases rather than additional ordinary C implementations.
Source identity and semantic deltas precede candidate counting.

The root LICENSE supplies a BSD 3-Clause grant; README describes the 2015
release. Earlier MWC confidentiality/copying notices remain in historical files.
The release grant and original notices require explicit provenance reconciliation
before import; the root grant cannot silently erase third-party exceptions.
Henry Spencer regex files carry a separate University of Toronto statement
requiring truthful origin and modification notices. Preserve each lineage.

| Pinned source path | SHA-256 | Investigation |
| --- | --- | --- |
| `LICENSE` | `0f3f862b476757802bc02cab60d696be66731aa8b02e305310a6bd9803afbce7` | Root BSD 3-Clause grant; preserve per-file notices |
| `README.md` | `7c01adc328a629f62b34c9726b965b9bf946baeb838ea038cdd72e083ee24ccc` | Historical release provenance |
| `mwc/romana/relic/b/lib/libc/stdio/Makefile` | `6ef93d9a50ff19775dcc01f80b4f971fd7c0d203ec1d95a02213001725e8ab44` | Optional formatting/cleanup archive selection |
| `mwc/romana/source/4.2.x/usr/include/stdio.h` | `164878352e4bab70b13b5ce7be0ae884364ad02be06886d31a30f4ffed580780` | Hot FILE fields and auxiliary per-stream dispatch |
| `mwc/romana/relic/b/lib/libc/stdio/_finit.c` | `c4d4f49fa86ea97e44d9773513988471f046232ef1ae239315f0a24418172a7e` | Lazy buffering and operation-state transitions |
| `mwc/romana/source/4.2.x/usr/src/misc/regexp.c` | `145883595e1d5cc63f119681f3a3273b532fb5800925bede0d956501439c886a` | Two-pass regex compilation and rejection metadata |
| `mwc/romana/relic/b/lib/libc/gen/qsort.c` | `3d4a07fddd50f1bec9a33a1bda35ca3b4ec9221d168ee6205d8ff809bd96502d` | Explicit partition work stack and pivot policy |
| `mwc/romana/relic/b/lib/libc/stdio/_scanf.c` | `50686d67756f9d6e8f016e25564708878509f360560ca73db55a01dfa1463c5e` | Fixed scratch capacity and scanset admission |
| `mwc/romana/relic/b/lib/libc/stdio/_dscan.c` | `2feaf7761d2984118c940acd0c6da778654932fd5e50d42f554b58cd863d3ad0` | Floating-token state machine |
| `mwc/romana/relic/b/lib/libc/gen/strtod.c` | `dc3f80bf1b63fa017ecda1766207d2e55f8941bc91bdbd61010d598f6826e339` | Integer-chunk decimal accumulation |
| `mwc/romana/relic/b/kernel/coh.386/exec.c` | `a298fe4e70279b54872c7305e5e0e013deedb26046653798a7f157435c7320c4` | Exec admission, argument stack and commitment |
| `mwc/romana/relic/b/kernel/coh.386/proc.c` | `4b7c0498c7590a72a6adbbd5223de6ded584cd0d93ff93b836ad5e3dcb39cd6e` | Fork resource staging and rollback |
| `mwc/romana/relic/b/kernel/coh.386/alloc.c` | `3b4ac02e7bdc19bb11ed660684920c6194edb60aa85c079ad2397e3b9e107cc0` | Arena sentinel, coalescing and zeroing |
| `mwc/romana/relic/b/kernel/coh.386/var.c` | `9ed42162a3b24ff0471ab3f2c93c968bb5b3206b5b10628adc358790f8b4a029` | Syscall argument and return metadata |
| `mwc/romana/relic/b/kernel/tools/coffnlist.c` | `c4f3f96c1e28657b8f5feb3ffb7e9f30b030e2cb3923121a5d2a566ac8ded5ff` | Object-symbol reader and native-width hazards |
| `mwc/romana/source/4.2.x/usr/include/coff.h` | `dffc0ff8646e88ed3d1567a74f806e2dfcb8405765585468f1d6cf21bf67656c` | Serialized object ABI declarations |
| `mwc/romana/relic/b/kernel/coh.386/Makefile` | `cfedd864356fa258fdfaf778d2ca4daade0d048203c9b3925a3db55f3e66d9b9` | Native build dependency ownership |
| `mwc/romana/relic/d/etc/mkfs.c` | `7627d8f6a50fe8858b0aad97e57a3536f5274c6afa739035662ab8b42e611e21` | Image planning before filesystem emission |


## Reusable invariants and falsifiers

1. **Archive selection measures complete costs.** The stdio Makefile documents
   real/fake floating formatter and exit-cleanup selection through archive order
   and forced undefined symbols. Fake output cannot silently implement C17
   printf. Build tiny `_Exit`, exit, integer/floating printf and file-I/O
   consumers; retain extracted members and text/data/BSS. Test incremental
   archive replacement with the actual linker. Import dependency accounting,
   not historically reduced behavior.

2. **Per-stream dispatch separates hot fields from backend state.** FILE keeps
   counters/pointers on the byte path while `_FILE2` carries get/put callbacks,
   bounds and auxiliary state. A memory-stream backend can dispatch at
   refill/flush boundaries rather than branch for each byte. Auxiliary storage
   and indirect calls have real costs. Compare complete stream residency,
   sizeof/alignment, ungetc, EOF/error and mixed-backend behavior on Thumb-1.
   A smaller public structure alone establishes no memory savings.

3. **Lazy initialization is an ownership transition.** `_fginit` and `_fpinit`
   configure buffering on first use and replace callbacks. Tty selection,
   explicit setvbuf, errno, update streams and allocation failure belong in the
   contract. The donor changes to unbuffered operation after allocation failure
   and has an errno-restoration path worth testing. Measure untouched-stream
   storage separately from used-stream storage; rederive error behavior.

4. **Regex metadata proves conservative rejection only.** A first compile pass
   checks syntax and size; a second emits into one allocation. Compact offsets
   and a 32767 guard constrain program size. Anchoring, first-character and
   required-literal metadata reject impossible matches. Disable each filter
   and exhaust finite alphabets to detect false negatives. Test malformed and
   oversized programs, allocation failure, nested compilation, work/stack
   budgets and POSIX longest-match/capture counterexamples. Static compile
   state and recursive backtracking leave complete POSIX and linearity claims open.

5. **Logarithmic depth can carry a large constant.** The sort uses
   `STACK stack[CHAR_BIT * sizeof(size_t)]`, processes the smaller partition and
   queues the larger. Three 32-bit fields times 32 entries imply 384 bytes for
   the array alone on ILP32. DiscoBSD already bounds recursion; explicit storage
   needs measured justification. Bad-partition sampling and shellsort fallback
   do not prove introspective worst-case bounds. Count comparisons and queued
   partitions on duplicate-heavy/adversarial inputs, and test empty/zero-size
   cases, nested contexts and pointer formation when popping the stack.

6. **Token capacity includes termination.** `_scanf` uses a 128-byte buffer,
   caps numeric width at 128 and appends a NUL; an accepted 128-character token
   needs a separate termination byte. Scanset expansion creates another bound.
   `_dscan` separates lexical recognition from assignment, but its rollback
   commentary cannot replace normative input-item rules. Test lengths
   127/128/129, every scanset byte, incomplete prefixes/exponents followed by
   `%c`/`%n`, and signed-char ctype domains.

7. **Integer chunks can reduce software floating operations.** strtod maintains
   a relation of the form `d * 10^vdigits + val` and postpones floating work
   while integer accumulation fits. Count target helper calls to test the
   performance hypothesis. Correct rounding, hex forms, long exponents,
   halfway values, subnormals, signed zero, errno and end pointers require an
   independent high-precision oracle. Host-double agreement is insufficient.

8. **Exec has a named commitment boundary.** The source validates the object
   and builds arguments before releasing original segments. DiscoBSD cannot
   assume simultaneous old/new images in one flat window; adapt admission,
   resource reservation and ownership ordering. Test invalid headers, all
   intermediate sum/product/rounding overflows, empty vectors, unterminated
   strings, alignment, argument exhaustion and failures on either side of
   commitment. The extra-section loop iterates `xp` while a displayed call uses
   `xlist->xseg`, supplying a multiple-section adversarial fixture.

9. **Runnable state follows complete fork construction.** Child allocation and
   segment duplication precede scheduling visibility; failures release acquired
   ownership. Fault-inject each acquisition and compare reference counts and
   runnable state. x86 context return and Coherent segments remain outside the
   flat-window swap model.

10. **Arena conservation precedes fit policy.** A sentinel bounds allocation;
    search coalesces free blocks, prefers exact fits, then splits larger blocks
    and clears returned storage. Pointer tagging, vtop, size rounding and stack
    diagnostics require a target model. Test alignment, overlap, overflow,
    fragmentation, double free, sentinel integrity and zeroing. Measure
    worst-case scan latency under the selected exclusion boundary.

11. **Metadata joins every ABI consumer.** The syscall table combines argument
    count, return kind and handler. Replace unprototyped callbacks through one
    typed description; verify stubs, dispatch, widths and invalid slots. COFF
    structures use native long and sizeof for serialized layout. The object
    reader supplies negative fixtures for explicit-width decoding: truncated
    headers, overflowing ranges, auxiliary-symbol overruns, invalid string
    offsets and missing terminators.

12. **Plan images before publication.** mkfs scans a prototype and allocates
    blocks before emission. Reuse planning/admission discipline without
    importing its filesystem format or device-write path. Test deterministic
    timestamps, capacity failure before output commitment, duplicate paths,
    malformed prototypes, depth limits, block conservation and independent
    read-back. Historical NDEBUG and time calls need explicit target treatment.

## Integration order and replay

The canonical C17 migration queue retains dependency order. Coherent enriches
sorting, FILE/memory streams, numeric conversion, regex, generated ABI,
exec/fork admission and image construction. Each candidate remains open until
contract/caller closure, C17/native-input acceptance, license provenance and
resource evidence satisfy the existing gates. Explicit work stacks, two-pass
compilers, conservative filters and chunked conversion have established prior
art; novelty claims concern only justified port-specific compositions.

Given a caller-selected `COHERENT_DONOR` checkout, replay an identity without
relying on working-tree bytes:

```sh
set -eu
: "${COHERENT_DONOR:?select the Coherent clone}"
git -C "$COHERENT_DONOR" show a00b86ea22c66625cf1838a71c06129f5eda2b49:mwc/romana/source/4.2.x/usr/include/stdio.h > coherent-stdio.source
sha256sum coherent-stdio.source
```

The command writes the named replay output in the caller's current directory. Compare its digest with the table and retain source identity beside adaptation evidence. Full Git history, RCS extraction, historical binary disassembly and COHERENT.pdf analysis remain follow-up surfaces rather than completed review.
