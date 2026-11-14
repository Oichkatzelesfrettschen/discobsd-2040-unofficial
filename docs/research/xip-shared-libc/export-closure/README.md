# Shared-libc API roots and archive provider closure

`tools/libc_shared_closure.py` checks whether a compiled ARM archive supplies an
explicit API contract and every strong dependency of its selected members.
Installed-program reachability supplies workload evidence; it cannot define the
library's public API because an application can call an interface that shipped
programs leave unused. The checker makes that distinction executable.

## Contract and fixed point

The JSON contract has exactly three fields. Each root names a function or
object, and each external symbol names its owner and a reason:

```json
{
  "schema": 1,
  "roots": [
    {"symbol": "qsort_r", "kind": "function", "reason": "Selected Issue 8 API"}
  ],
  "external_symbols": []
}
```

For required symbols R and selected members M, resolution starts with M empty.
Each required symbol contributes its resolved provider member and that member's
strong undefined symbols. Every member enters M at most once, so cycles reach
a finite fixed point. Verification checks the complete selected set again,
including required kinds, conflicting providers and unresolved dependencies.
Input-section accounting counts each member once, including BSS separately.

The provider policy prefers a unique strong definition over a weak fallback.
Multiple providers require an explicit ownership decision unless their complete
COMDAT group identities agree. Equivalent groups use the lexicographically first
member as the deterministic candidate provider; ordinary duplicate strong or
weak definitions still fail. This policy constructs a candidate closure; it
does not emulate a static linker's order-dependent archive extraction. Weak
imports remain optional and visible. A named external owner can satisfy a strong
dependency, but cannot substitute for a public API root or an archive definition.
An external declaration records a boundary; it proves no runtime provider.

The inspector reads member bytes with `ar p`, without filesystem extraction.
Regular embedded archives bind member contents to the reported SHA-256. Thin
archives fail because their referenced files require a separate identity model.
The inspector rejects empty, duplicate, unsafe and case-alias member names,
COMMON storage and unsupported ELF targets. Explicit ELF object types remain
objects even in executable sections. Untyped assembly symbols use section flags
as a classification heuristic and require later ABI review.

## COMDAT ownership and accounting

The [ELF ABI section-group rules](https://gabi.xinuos.com/elf/03-sheader.html#section-groups)
define a group as an indivisible linker selection unit. The linker may discard
duplicate groups by signature. The inspector additionally requires equal
normalized group contents before it treats their providers as interchangeable;
it rejects differing implementations even when the linker accepts their common
signature. This is an ownership policy for reproducible closure, rather than a
claim that every rejected group is invalid ELF.

The normalized identity includes section names, types, flags, alignment, entry
sizes, contents, exported symbol layouts and visibility, and relocation types,
addends and target identities. Symbol-table indexes do not enter that identity.
Local symbols within a group resolve through their section and offset. Local
references outside the group retain their archive-member owner, so two groups
that address different private state cannot silently coalesce. Initialized and
zero-fill storage both contribute to the group identity.

The inspector requires exclusive group membership, valid COMDAT metadata and
relocation ownership in both directions. A relocation section outside a group
cannot modify storage inside it. References from outside a group to its local
symbols or through section-header indexes also fail. Groups support ordinary
PROGBITS/NOBITS storage and REL/RELA relocations; unsupported storage metadata
fails explicitly. Different group definitions with the same signature fail
even when their exported symbol names do not overlap.

`input_section_bytes` preserves gross selected-member accounting.
`coalesced_input_section_bytes` subtracts duplicate group storage once per
signature; it leaves ordinary member storage unchanged. These fields are input
costs before garbage collection, relaxation, output alignment and final image
placement. They establish no final flash or process-state size. The JSON output
also records normalized group digests and per-group input-section costs.

## Replay and calibration

The root gate compiles real ARM objects and archives and exercises successful
closures and rejection controls. Dependencies and CI ownership live in
[TESTING.md](../../../../sys/arch/rp2040/doc/TESTING.md). GCC 16.2.0, binutils
2.47.20260726 and pyelftools 0.33 produce the retained local screen; hosted CI tests its
own installed versions. A missing dependency fails rather than skipping.

```sh
export PYTHON=/usr/bin/python
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-libc-shared-closure
"$PYTHON" tools/libc_shared_closure.py lib/libc.a --describe
"$PYTHON" tools/libc_shared_closure.py lib/libc.a --contract contract.json
"$PYTHON" tools/libc_shared_closure.py lib/libc.a --contract contract.json \
    --members members.json
```

`--ar` selects the archiver. `members.json` contains an explicit JSON list of
member names. Contract fields, symbols and reasons are validated; duplicate
JSON fields fail. Successful output contains the archive hash, normalized
contract, member set, unresolved weak symbols, named external imports and input
section sums. Diagnostics go to stderr with a nonzero exit status.

Thirty-three tests cover transitive dependencies, dormant APIs, missing dependencies,
cycles, optional weak imports, external owners, object/function confusion,
provider collisions, duplicate/case-alias/unsafe member names, objects in text,
strong-over-weak selection, malformed contracts, thin archives, COMMON storage,
ambiguous weak providers, deterministic accounting and the CLI JSON boundary.
The thin-archive control requires the explicit embedded-archive diagnostic.
The precheck also makes archive identity independent of `ar` path formatting.
COMDAT cases compare coalesced section sizes with GNU `ld -r`, resolve the
production shared syscall wrappers, and distinguish code, relocation targets,
visibility, RELA addends and private-state owners. Negative controls cover group metadata,
relocations outside group ownership, incoming local-symbol and section-index
references, and signature collisions with disjoint exports. Zero-fill storage
counts once, archive reversal preserves the chosen equivalent provider, and
group signatures retain the ELF naming domain independently of API identifiers.

The [COMDAT qualification receipt](comdat-qualification.json) binds the 33-test
run, the bounded review and its repaired relocation-ownership finding, and the
ELF specification snapshot. The existing 449-member static archive retains
identical provider metadata and gross costs, apart from additive empty group
metadata. Shared syscall selection has an explicit two-wrapper witness; broader
shared-archive build wiring and ABI selection remain open.

## Reproducing the all-global screen

The following discovery contract selects every defined global; its reasons
explicitly leave public ABI eligibility unreviewed. Run after a clean build
at the receipt's revision to compare costs. The external-owner map remains
explicit, so a new unresolved symbol causes rejection.

```sh
"$PYTHON" tools/libc_shared_closure.py lib/libc.a --describe > .build-logs/shared-inventory.json
"$PYTHON" - <<'PYTHON_SCREEN'
import json
from pathlib import Path
inventory = json.loads(Path('.build-logs/shared-inventory.json').read_text())
roots = []
for symbol, providers in sorted(inventory['providers'].items()):
    definitions = [inventory['members'][name]['definitions'][symbol]
                   for name in providers]
    strong = [row for row in definitions if row['binding'] == 'STB_GLOBAL']
    definition = (strong or definitions)[0]
    roots.append({'symbol': symbol, 'kind': definition['kind'],
                  'reason': 'Existing global; public ABI eligibility unreviewed.'})
helpers = ('__aeabi_llsl', '__aeabi_llsr', '__gnu_thumb1_case_sqi',
           '__gnu_thumb1_case_uhi', '__gnu_thumb1_case_uqi')
startup = ('__progname', '_end', 'environ')
external = [{'symbol': name, 'owner': owner, 'reason': 'Explicit screen boundary'}
            for owner, names in (('compiler', helpers), ('startup', startup))
            for name in names]
contract = {'schema': 1, 'roots': roots, 'external_symbols': external}
Path('.build-logs/shared-contract.json').write_text(json.dumps(contract) + '\n')
PYTHON_SCREEN
"$PYTHON" tools/libc_shared_closure.py lib/libc.a \
    --contract .build-logs/shared-contract.json > .build-logs/shared-closure.json
```

## Production-archive screening

[screening.json](screening.json) binds the source revision, archive hash,
toolchain, input-section sums and retained historical selection identity.
The clean archive has 423 members and 614 defined globals. A hypothetical
contract that selects every existing global resolves to 422 members, excluding
`fakcu.o`: `findiop.o` supplies the strong `_cleanup` definition over the weak
empty fallback. Public eligibility remains unreviewed for every screening root.

| Input sections | Bytes |
| --- | ---: |
| `.text` | 50,140 |
| `.rodata` | 1,801 |
| `.rodata.str1.1` | 1,806 |
| `.data` | 795 |
| `.bss` | 3,749 |

Input text and read-only data total 53,747 bytes, 4,595 above the fixed 49,152-byte
reservation. The sum excludes final link alignment, PIC transformations,
garbage collection, veneers, stable slots and initialization-image placement.
It supplies neither an exact final size nor a lower bound. The partition stays
fixed while API selection and actual PIC linking decide fit.

The eight strong external dependencies are compiler helpers `__aeabi_llsl`,
`__aeabi_llsr`, `__gnu_thumb1_case_sqi`, `__gnu_thumb1_case_uhi`,
`__gnu_thumb1_case_uqi`, and startup-owned `__progname`, `_end`, `environ`.
Their owner declarations remain prerequisites for the full ABI implementation.

The retained earlier installed-program selection contains 263 members. Applying
that set to the clean archive with the all-global contract rejects missing
`_Exit`. Member comparison also shows `strnlen.o`, `strndup.o`, `stpcpy.o`,
`stpncpy.o`, `at_quick_exit.o`, `quick_exit.o`, `iso_exit.o` and `qsort_r.o`
outside the selection. The receipt records the earlier selection's revision;
the comparison establishes its API omissions, not current program linkage.

An incremental archive retains removed `_Exit.o` alongside `iso_exit.o` and
`_exit.o`, and the checker rejects the case alias. A clean build removes that
stale member. This observation supports the existing clean-rebuild requirement
for renamed libc members; an incremental `ar cr` update preserves omitted ones.

## Remaining ABI work

The [declaration frontier](../../shared-libc-declaration-frontier.md) compares
eight public-header roots with the compiled libc and libm providers. It records
unprovided declarations and an executable signed-zero counterexample in the
omitted legacy `fabs` source. Its bounded discovery set leaves export selection
and the remaining headers open.

The next contract derives selected public roots from reviewed declarations and
the compatibility profile. Complete signatures, visibility, symbol eligibility,
mutable-object alignment, stable slots, PIC provider closure, syscall/ROM-float
state ownership, native veneers and versioned kernel admission remain separate
obligations. Final linked bytes and process state costs determine admission.

The contribution is a calibrated composition of explicit API roots, compiled
provider ownership and separate resource accounting under the DiscoBSD ABI.
Graph reachability, weak symbol resolution and archive inspection have
established prior art. The checker claims neither a new algorithm nor a
complete shared-libc implementation.

## Full-archive PIC compilation

[The PIC input-section experiment](pic-input-sections.md) maps every shipped
ARM libc member to captured inputs, recompiles 441 members, and identifies
absolute assembly references that still require process-local state access.
Its section sums remain pre-link measurements; they do not establish that the
shared image fits the reserved flash interval.
