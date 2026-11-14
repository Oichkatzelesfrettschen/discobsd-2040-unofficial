# Libc partition and shared syscall mechanism costs

Two equal-band sorting candidates pass calibrated host execution but increase
sorted-input comparison counts. Shared syscall error factoring saves 976 linked
flash bytes while preserving all 125 static objects byte-for-byte. The shared
image still exceeds its fixed 48 KiB reservation. Neither experiment changes
production code or establishes hardware performance.

The [evidence receipt](libc-mechanism-cost-evidence.json) binds retained local
artifacts by SHA-256. The sorting acceptance runs against main
`c397f6e61ba146f0ca9d56178bdc39a75b3565ab`; shared compilation uses
`e36425ce00fefeab8ee78fefbe8a872c51645c4e`. The intervening merge changes
integration metadata and documentation, with unchanged libc implementation.
The retained scripts depend on captured compile commands and intermediate
artifacts; they are research experiments rather than installed replay gates.

## Equal bands bound duplicate work but change partition geometry

The [production partition analysis](qsort-partition-cost.md) identifies
alternating-key scans that retain almost the entire partition. The candidates
pin a real array element as pivot and maintain lesser, equal, unknown and
greater ranges. Each comparison removes one unknown element. Removing the
whole equal range eliminates that duplicate-driven chain. Smaller-side
recursion bounds live frames; a budget of twice the floor of log2(count)
decreases on both recursive and iterative partitions. Budget exhaustion uses
an allocation-free heap. Leaves of at most eight elements use insertion sort.

The median variant samples first, middle and last elements and moves the
selected pivot to the first slot. Its remaining partition mechanism is identical.
Both variants preserve typed ordinary and context-last callbacks and per-call
state. They independently implement established mechanisms; these results make
no claim of a new sorting algorithm.

| Measure | Production | First pivot | Median of three |
| --- | ---: | ---: | ---: |
| ARM ordinary text bytes | 480 | 472 | 558 |
| ARM context text bytes | 512 | 502 | 578 |
| Ascending comparisons, 4,096 records | 45,071 | 179,758 | 142,833 |
| Descending comparisons, 4,096 records | 45,072 | 179,511 | 175,713 |
| Alternating two-key comparisons, 4,096 records | 4,204,550 | 6,142 | 6,146 |

The candidates eliminate the production division-helper import. Their local
partition frame is 48 bytes, heap frame 40 bytes and byte-swap frame 12 bytes.
Smaller text therefore supplies no blanket stack-saving claim. Median sampling
only partly reduces the sorted-input regression; pivot selection alone does
not explain the full difference. Neither candidate receives production
admission. A bidirectional equal-band partition with the same depth budget is
the next discriminating implementation experiment.

Each candidate passes four compiler profiles: GCC, native ILP32 GCC, Clang,
and Clang AddressSanitizer/UndefinedBehaviorSanitizer. Each profile exercises
78,740 existing context cases, 177,146 exhaustive ternary cases and 240 larger
pattern/budget cases. Seven calibrated controls exercise order, multiplicity,
comparison counting, recursion, fallback and pointer bounds/alignment oracles.
LeakSanitizer remains disabled because the sandbox prevents its ptrace path;
the algorithms allocate no memory. These executions establish exercised
behavior and comparison costs, separately from target cycle or stack measures.

An overlapping preliminary run supplies no acceptance evidence. A later frozen
run fails while compiling a negative control because GCC rejects a retained
local-array pointer. The repaired oracle stores an integer address and checks
integer bounds and alignment before dereference. The final parent acceptance
passes with identical before/after input hashes; the earlier failures remain
retained evidence.

## Compiler size flags preserve the same linked image

Fresh compilation of all 449 selected archive members under `-Os` and `-Oz`
produces links with the same 646 required roots and identical ELF SHA-256.
Both occupy 62,220 flash bytes and 5,344 writable-state bytes, including 1,536
initialized bytes and 648 GOT bytes. The comparison rejects compiler size-flag
selection as a demonstrated saving for this compiler and input set.

The linked text section occupies 57,372 bytes. Removing separate read-only
and initialization data alone therefore leaves text 8,220 bytes over 48 KiB.
That arithmetic bounds a storage-only proposal; it supplies no impossibility
claim about instruction reduction or a separately justified ABI selection.

## Shared error factoring retains process-local errno

The shared-only error expansion loads a Thumb helper address into r1 and
branches without replacing LR. One hidden helper uses r9 to load the active
process's errno address from its GOT, stores the kernel error value, returns
minus one, and preserves the caller's stack and return address. Static
expansions remain byte-identical across all 125 modified assembly members.

Ptrace clears errno before its syscall because minus one can also represent
success. Its prologue needs a distinct GOT-offset literal. The shared helper
pointer cannot occupy that literal's numeric label. The candidate retains the
errno literal explicitly and assigns the helper pointer a separate label.

The full link retains 646 roots, occupies 61,244 flash bytes and preserves
5,344 state bytes. Its 976-byte reduction leaves 12,092 bytes over the fixed
reservation before function slots. Savings do not authorize partition growth.

Unicorn executes 32 modeled calls: success and error paths for read, write,
open, close, _brk, pipe, ptrace and sigaction across two process-state areas.
The model injects explicit syscall results and carry flags. Checks cover errno,
callee-saved registers, r9, SP, LR, inactive-state preservation, pipe outputs,
ptrace arguments and successful minus-one results, and sigaction's trampoline
argument. Flash remains read/execute during execution. Five controls reject an
even helper address, wrong helper target, omitted state relocation, wrong
process base and ptrace literal aliasing. Mutation-construction failures remain
fatal rather than counting as successful rejection. A separate fault injection
raises a Unicorn memory error while constructing the first helper mutation;
the experiment terminates before assigning that mutation a verdict.

The model supplies instruction-execution evidence under injected syscall
outcomes. Kernel exception entry, signals, fork, swap restoration and board
latency remain unqualified by this experiment. Production adoption also requires
helper build ownership, the object-ABI gate's relocation-oracle migration,
calibrated production tests and matched resource qualification. The
[shared-libc contract](xip-shared-libc/README.md) retains the full admission and
lifecycle requirements.
