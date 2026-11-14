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

The clean distribution and dependency capture decide whether existing consumers
preserve their compiled behavior and resource costs. Standalone acceptance
alone establishes no whole-system resource result. The full standards ledger
and shared-library ABI selection retain their independent denominators.
