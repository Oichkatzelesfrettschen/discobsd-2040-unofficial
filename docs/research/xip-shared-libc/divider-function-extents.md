# Divider function extents

The shared callable-owner checks require a provider's function extent alongside
its section and Thumb entry. A current static-archive census at main `7d6e357b`
finds 613 global/weak function definitions and 89 local function definitions
in 449 members. Exactly four global definitions have zero sizes:
`__aeabi_uidiv`, `__aeabi_uidivmod`, `__aeabi_idiv` and `__aeabi_idivmod` in
`aeabi_div.o`. The two unsigned names share an entry; the signed names share
the next entry. Zero size is legal ELF metadata, so the finding supplies an
ownership prerequisite rather than a C/POSIX defect.

## Requirement and deciding checks

The assembly supplies each alias with the exact code extent of its entry.
The existing `--user-helper` verifier derives independent production bounds
from successive entries and the final text boundary. All four extents must
match their owners. Positive, missing, unequal and escaping metadata controls
calibrate that check. Existing arithmetic and divider-window witnesses retain
their own contracts and oracles.

The metadata-only hypothesis predicts identical allocated bytes, relocation
records, entry addresses and converted native a.out output before and after
the change, under production and `DIVIDER_MODEL` assembly. The model's literal
pool lies outside the function extent. A changed instruction, data byte,
relocation or native output falsifies that hypothesis. No hardware execution
forms part of these checks.

## Implementation and qualification

- [x] Add the extent oracle and record its rejection of baseline zero sizes.
- [x] Add assembly extents at the code boundaries while preserving all inherited
  source text and instruction sequences.
- [x] Calibrate missing, unequal and escaping extents alongside existing timing,
  access-order, call, stack and missing-entry controls.
- [x] Compare production/model object contents and native assembly outputs;
  execute existing arithmetic-model and adjacent shared-owner gates.
- [ ] Record source and tool identities, review, measured resource disposition
  and integration through the owned remote.

The rebuilt archive contains 702 positive, section-contained function
definitions: 611 global, two weak and 89 local. The census also finds zero
untyped global/weak definitions in executable sections. The distinction between
613 public/link-visible definitions and 702 total definitions fixes the census
denominator; the four added extents close the same four missing owners.

| Alias pair | Production extent | `DIVIDER_MODEL` extent |
| --- | ---: | ---: |
| `__aeabi_uidiv`, `__aeabi_uidivmod` | 32 bytes | 38 bytes |
| `__aeabi_idiv`, `__aeabi_idivmod` | 60 bytes | 66 bytes |

Each `.size` precedes the optional `.pool`. Independent disassembly of the
declared ranges ends at `bx lr`; the model's literal-data mapping symbols lie
outside those ranges. The production `--user-helper` oracle derives bounds
without using `st_size`. Its baseline run reports four zero-size findings.
The six metadata controls reject missing, short, overlapping and escaping sizes,
a valid entry substituted for the unsigned alias, and a cleared Thumb bit.
Each control preserves section contents outside `.symtab` and requires exit 1
with its named diagnostic. Nine existing instruction/window controls also reject.

## Object and archive identity

The [measurement receipt](divider-function-extents-measurements.json) binds
the baseline commit, source digests, object identities and matched archive costs.
The candidate source differs from baseline `7d6e357b` by exactly four `.size`
directives. The GNU ELF comparison clears only those four `st_size` fields
before whole-object equality, so code, relocations, symbol indexes and entries
retain their identities. Changed text, relocation and native-object bytes each
fail a calibrated identity control.

| Build route | Production bytes, before/after | Model bytes, before/after | Identity |
| --- | ---: | ---: | --- |
| ARM GNU ELF | 776 / 776 | 1,028 / 1,028 | Only four size fields differ |
| Full native assembler route | 208 / 208 | 308 / 308 | Byte-identical |
| Selected native assembler route | 208 / 208 | 308 / 308 | Byte-identical |

The full native route invokes GCC with `-B tools/bin/rp2040/` and `-Wa,-x`.
The selected native route preprocesses with GCC `-E -P` and invokes the native
assembler with `-X`, as `distrib/rp2040/mkboardlibc.py` does. Both routes use
`-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -x assembler-with-cpp`;
`-DDIVIDER_MODEL=1` selects the model. Input from stdin keeps source filenames
equal during object comparisons. Native archive production uses these assembler
routes; `elf2aout` owns executable conversion.

Fresh matched selected-native archives each occupy 51,178 bytes and contain
175 entries including archive bookkeeping. Their payloads and headers match
after normalizing only member timestamps. The pre-existing 51,190-byte cached
archive supplies no matched baseline; its refresh difference stays outside
the patch's resource accounting. Production ELF libc remains 517,900 bytes;
its divider member remains 776 bytes, with four changed size-field bytes.
The full native archive remains 126,998 bytes and its 208-byte divider member
is identical. Whole full-native archive byte identity remains unclaimed because
its unnormalized headers differ.

These comparisons establish zero size growth and unchanged executable input
bytes for the measured source pair. They supply no current-head distribution
capacity or workload timing measurement. The metadata change adds no instruction,
allocation or stack operation; shared image fit retains its separate size gate.

## Replay and evidence classes

Select and export `PYTHON`, build the RP2040 tools, then run the integrated
assembler fixture and the independent arithmetic model:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-cross-assembler
REQUIRE_QEMU=1 bmake -C tests/rp2040/divider_helpers MACHINE=rp2040 check
bmake MACHINE=rp2040 check-libc-aout-contracts check-libc-xip-slots \
    check-libc-shared-closure check-c17-source-denominator
```

The arithmetic QEMU model reports 480,800 comparisons, 356,220 modeled steps,
zero failures and eleven rejected arithmetic controls. The adjacent slot gate
reports four images, 888 modeled calls and seventeen rejected controls; the
closure checker reports 33 passing tests. Native archive checks reject 36
missing-member controls. Ruff, ShellCheck at error severity and focused mypy
cover the changed Python and shell tools. The existing cross tier owns the
extent fixture through `check-cross-assembler`; its Makefile, testing table and
firmware CI comment identify that ownership. No additional root target exists.

Retained local evidence lives under `.build-logs/divider-extents/`: the four
`*-launch.log` files, `oracle-red.stderr`, `census.json`,
`identity-all-native-routes/receipt.json`, `selected-native-pair/receipt.json`
and `production-baseline/receipt.json`. Unique
`.build-logs/divider-extent-controls-*` directories retain metadata controls.
The committed measurement receipt sanitizes local paths and records immutable
source hashes. Final committed-source qualification and bounded review remain
required before integration; the measurements above originate from the candidate
source hashes recorded in that receipt.

The generated ABI description, private-alias generation, full export selection,
48 KiB image fit, native shared linking, exec admission and lifecycle remain
separate implementation work. Accurate ELF extents establish ownership metadata;
they establish no new arithmetic semantics or physical-divider timing result.
