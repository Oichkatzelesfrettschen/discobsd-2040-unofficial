# Network header inclusion contracts

## Requirement and falsifier

`netinet/in.h` supplies its type prerequisites and both network headers tolerate
repeated and reordered inclusion. `arpa/inet.h` exposes a complete `in_addr`
through its owning header. Existing ARM layouts, function signatures and
byte-order macros remain stable. POSIX type additions and signature changes
remain separate requirements.

The baseline at `12b1410c` rejects standalone `netinet/in.h` because `u_long`
and `u_short` lack declarations. Repeated inclusion redefines its structures.
Repeated `arpa/inet.h` expands the byte-order macros inside its own function
declarations. The repair introduces inclusion guards and the prerequisite
chain `arpa/inet.h` to `netinet/in.h` to `sys/types.h`. A rejected valid include
order, changed target layout or surviving mutation falsifies the repair.

## Calibrated compiler evidence

`check-network-header-contracts` tests twelve inclusion forms across production
GNU17, GNU strict C17 and Clang strict C17 with four namespace selectors:
144 valid inputs. Each profile rejects two removed guards, two removed
prerequisites, a changed address-field type and a changed socket layout.
The gate requires the expected diagnostic for each of those 18 rejections;
synthetic process results calibrate the diagnostic matcher.

The modern compiler fixtures check structure sizes, alignments, field offsets
and field types. They also check ten existing function-pointer signatures,
bypassing byte-order macros for address-taking, and constant byte-order
values. Syntax-only acceptance checks variable-call expressions; it supplies
neither linked providers nor runtime semantics.

Forty-eight cross-preprocessed Smaller C consumers compile and assemble for
Cortex-M0+. They check structure sizes, function-pointer assignments and
variable byte-order expressions. A malformed input and diagnostic controls
calibrate the native compiler verdict. Native preprocessing, execution and
physical-board behavior remain outside these inputs.

The parent acceptance run reproduces the delegate's counts and passes Ruff.
The [testing inventory](../../sys/arch/rp2040/doc/TESTING.md) owns tool
requirements, execution classes and CI registration through `check-cross`.
The verifier records input and tool identities and individual verdicts.

```sh
: "${PYTHON:?select the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-network-header-contracts
```

## Caller and resource qualification

Candidate `ea2bb788` includes the wide-time main revision `c2ffd2d8`. Separate
clean distributions and complete shipped dependency closures supply the
[baseline](network-header-resource-baseline.json),
[candidate](network-header-resources.json), and
[comparison](network-header-resource-comparison.json). All 162 measured resource
surfaces remain unchanged, with zero positive deltas and 236 free root blocks
in each image. The header change requires no resource exception. The selected
build manifest, `distrib/rp2040/_manifest`, supplies image membership; the raw
source manifest also contains optional closures absent from the selected image.

The [qualification record](network-header-qualification.json) binds source,
tool and artifact identities to each result. On `ea2bb788`, the network-header
gate repeats all 144 valid cases, 18 diagnostic-qualified rejections and 48
Smaller C inputs. Record-header contracts also pass. The time-binding gate
scans 952 ARM objects for 24 narrow time entry points and finds zero references.
The separate ILP32 execution inventory passes all 210 required recipes.

The full aggregate passes at initial candidate `e72af7bc` in 2,937.046 seconds,
including its build stage. That result precedes the wide-time main merge and
remains evidence for its recorded source. The merged candidate has fresh clean
builds, matched resources, focused header and time-binding checks, the ILP32
inventory, hosted CI and local Renode. A full aggregate run on that revision
remains open. The merge preserves both cross-contract registrations.

Local Renode boots the fresh PICO_UART kernel and root image, checks MPU faults,
exec residue, process state and injected core-1 stalls, and reads clock and file
times in 2046. Its classifier accepts 107,065 model warnings across 19 bounded
classes with zero errors. Hosted CI also passes on `ea2bb788`. Physical-board
execution remains unrun.

Two sandbox restrictions have separate diagnostic witnesses. LeakSanitizer
thread attachment fails with `EPERM` inside the sandbox and succeeds for the
same executable outside it. A minimal ILP32 executable receives `SIGSYS` inside
the sandbox and exits zero outside it. The unchanged aggregate and ILP32
inventory pass outside the sandbox; these results establish execution-environment
differences without suppressing their checks.

The retained installed-header scan at `e72af7bc` covers 182 paths in four modes.
Each mode improves from 143 to 144 clean headers and retains 38 failures.
`netinet/in.h` accounts for the improvement, with no newly failing header.
That discovery result precedes the wide-time merge. The full standards ledger,
remaining header contracts and shared-library ABI selection retain their
independent denominators.
