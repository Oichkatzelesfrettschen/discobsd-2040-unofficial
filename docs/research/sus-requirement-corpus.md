# SUS requirement corpus and tiny-system translation

This corpus gives DiscoBSD a finite standards surface without importing the
copyrighted standards text or another operating system's conformance verdicts.
It records 1,617 named entities from two locally retained official archives:

| Edition | Authority | Archive identity | Entity rows |
| --- | --- | --- | ---: |
| Issue 7 | IEEE Std 1003.1-2017 and The Open Group Base Specifications Issue 7, 2018 edition | `susv4-2018.tgz`, SHA-256 `ab6636bca53c7d71d33d2c5149ede574d598fe6ec97fa8b08e0459ef7bcfc104` | 1,465 |
| Issue 8 | IEEE Std 1003.1-2024 and The Open Group Base Specifications Issue 8, 2024 edition | `susv5.zip`, SHA-256 `9a1237e4b7d7b779c6013911377bebe632a7b6765d242eee51859db06355e3df` | 1,518 |

`sus-requirement-entities.tsv` contains only structural facts: entity kind,
standardized page name, archive member for each edition and the resulting
edition relationship. `sus-requirement-corpus.json` carries counts, hashes,
requirement facets, tiny-system translation rules and bounded mappings to
DiscoBSD evidence. `tools/check_sus_requirement_corpus.py` validates both
files and cross-checks all 107 names in the selected-image utility ledger.

The Issue 7 source and the original expanded ledgers came from XINIM commit
`3fe10dee69d657aef405cf62644a77aceee0f9fc`. XINIM's 1,483 Base Definitions
clause rows and 15,740 System Interfaces clause rows use Q35 Ring 3 witnesses.
Their entity keys and source identities inform this corpus. Their open or
closed state does not transfer to an ARMv6-M board. The Issue 8 archive was
found in two local holdings whose bytes have the same SHA-256; one identity is
recorded because duplicate storage adds no authority.

## Denominator

The union contains:

| Entity kind | Union | Issue 7 | Issue 8 |
| --- | ---: | ---: | ---: |
| Base Definitions pages | 104 | 95 | 100 |
| System Interface pages | 1,329 | 1,195 | 1,247 |
| Utility and special-builtin pages | 184 | 175 | 171 |
| Total | 1,617 | 1,465 | 1,518 |

Issue 7's 175 utility rows match the XINIM Shell and Utilities denominator:
160 standalone pages plus 15 special builtins derived from the Shell Command
Language chapter. The three general System Interfaces pages retained by the
XINIM parent derivation remain entities in both editions.

The JSON also pins XINIM's finer Issue 7 source-ledger structure without
copying normative prose: 1,483 Base Definitions clause keys, 15,740 System
Interfaces clause keys, 17 token-recognition requirements, 125 shell-grammar
requirements and the 2,644-row recursive shell/utility frontier. Counts and
ordered key hashes make those denominators durable. DiscoBSD expands a parent
into those finer rows when implementation work reaches it; a parent cannot
close while one required facet or child row remains open.

The edition relation reports archive-page presence. An `issue7-only` page is
not automatically a removed requirement, and an `issue8-only` page is not
automatically a mandatory Base feature. Issue 8 reorganizes pages, splits
interfaces, adds facilities and retires others. A row needs edition text and
option-mark analysis before its applicability changes.

Seven names in DiscoBSD's selected-image ledger -- `ftp`, `less`, `mail`,
`rmail`, `telnet`, `whereis` and `which` -- are BSD or historical extensions
outside the imported entity set. The validator names them explicitly so an
extension never closes a similarly shaped standard row.

## Requirement decomposition

A standards entity does not have one Boolean compliance bit. Its contract is
the product of edition, applicability, behavior and evidence:

```text
closure(entity) = edition source
                x selected option profile
                x required behavior facets
                x implementation ownership
                x deciding evidence
```

The multiplication is logical: one zero factor keeps the row open. Source
presence alone proves only the denominator. A declaration proves only an
interface shape. A linked symbol proves only reachability. A passing host test
proves only its exercised host conditions. Board behavior proves only the
measured image, silicon and workload.

The corpus assigns reusable behavior facets by entity kind:

- Base Definitions: types and layout, constants and limits, declarations,
  feature-test visibility, namespace and inclusion behavior.
- System Interfaces: declaration and types, argument domain, state transition,
  return value, errno and failure, resource ownership, and concurrency,
  signal or cancellation behavior.
- Utilities: synopsis, options, operands, input, environment, output,
  diagnostics, exit status and consequences of errors.

Each implementation mapping in `sus-requirement-corpus.json` selects the
facets it exercises and names its residual. This shape prevents a smoke test
from becoming a whole-page claim.

## Tiny-system translation

The RP2040 profile changes mechanisms while retaining observable behavior.
The following rules decide whether a reduction remains valid:

1. Preserve return values, errno, exit status, stream bytes, descriptor state
   and filesystem effects before reducing memory or packed-root blocks.
2. Reject input that cannot fit a fixed representation. Silent truncation
   followed by success changes the standard contract.
3. Share text through multicall dispatch and libc archive members. Preserve
   per-invocation state, per-stream state, cleanup and diagnostics.
4. Use the C and POSIX byte locale as the initial measured profile. Keep
   locale-dependent behavior partial until locale objects, collation and
   message catalogs exist.
5. Keep an unsupported Base requirement open. Mark an option unselected only
   after the edition source identifies the option boundary.
6. Separate host, ILP32, ARMv6-M link, native Smaller C and physical-board
   evidence. Each closes only the behavior it executes.

These rules expose a useful optimization boundary. Internal representation
can shrink aggressively when the observable-state vector stays invariant:

```text
observable = result + error + output + persistent effect + owned resources
```

Arithmetic ASCII folding removes tables because it preserves that vector in
the selected byte locale. Compact directory records preserve it because the
caller still owns stable selected entries. A smaller buffer that silently
clips a pathname changes the vector and therefore fails the translation.

## Current mapped surfaces

The JSON profile maps the following bounded or partial surfaces:

- compact stdio stream lifetime, modes, positions and formatted input;
- directory admission, close results, selected-entry ownership and byte-locale
  ordering;
- allocation-free ASCII case folding;
- the implemented `sysconf()` and `confstr()` selector subset;
- process, descriptor, signal and terminal mechanisms;
- selected textbox filters, filesystem utilities, the Bourne shell and the
  retained `awk` and `sed` implementations.

The mapping retains each residual. It does not claim full Issue 7 or Issue 8
conformance. Networking, asynchronous I/O, threads, realtime facilities,
memory mapping, general locale behavior and many interface selectors remain
in the denominator until an applicability decision and a deciding test exist.

## Reproduction

The ordinary repository gate validates the committed, copyright-safe corpus:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
"${PYTHON}" tools/check_sus_requirement_corpus.py --self-test
```

A holder of the two official personal-use archives can additionally prove the
committed rows against their source bytes:

```sh
"${PYTHON}" tools/check_sus_requirement_corpus.py \
    --issue7-archive /path/to/susv4-2018.tgz \
    --issue8-archive /path/to/susv5.zip \
    --self-test
```

Regeneration writes the TSV to standard output; the caller reviews the diff
before replacing the committed ledger:

```sh
"${PYTHON}" tools/check_sus_requirement_corpus.py --derive \
    --issue7-archive /path/to/susv4-2018.tgz \
    --issue8-archive /path/to/susv5.zip
```

The repository carries neither archive. The structural ledger and its hashes
remain reviewable, deterministic and sufficient for the default gate.
