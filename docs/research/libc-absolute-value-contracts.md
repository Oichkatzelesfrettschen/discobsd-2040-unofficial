# Callable integer absolute-value contracts

`abs` and `labs` return the mathematical magnitude in the argument's type.
C17 7.22.6.1 and POSIX Issue 8
[abs](https://pubs.opengroup.org/onlinepubs/9799919799/functions/abs.html)
and [labs](https://pubs.opengroup.org/onlinepubs/9799919799/functions/labs.html)
leave behavior undefined when that magnitude is unrepresentable. The defined
domain includes zero, positive values and every representable negative magnitude.
The implementation introduces neither saturation nor an error-reporting extension.

## Public symbols and build ownership

At source revision `a8e103eacb770a4c5da0bf68e3c2da7f9b4f0522`,
`include/stdlib.h` declares both functions, while `arm-none-eabi-nm` finds
neither provider in clean libc, libm or compiler libgcc. Freestanding address-taking
probes fail with each exact undefined symbol; an `imaxabs` control links.
The discovery commands use `rg -n 'abs|labs' include/stdlib.h lib/libc` and
`arm-none-eabi-nm --defined-only` on each archive. Built-in call expansion can
conceal this missing-provider class, so every behavior fixture takes function
addresses and disables built-ins.

`lib/libc/stdlib/abs.c` and `labs.c` provide typed conditional negation in separate
archive members. Each call owns its automatic input and returns directly;
neither function allocates, stores global state nor calls another function.
Separate members preserve independent static-link selection. The full ELF,
converted a.out and reduced native archives select both providers.
The retained `lib/libc/gen/abs.c` stays outside the maintained source selection.

The converted archive's source search includes `gen` before `stdlib`. Its explicit
`abs.o` recipe selects the typed source and names the local output `abs.o`.
BSD make can expand `$@` to an existing object found through `.PATH`; that path
names the cross-library ELF object. A build using that expansion replaces the
ELF object and then fails with `ar: abs.o: No such file or directory` in the
converted archive directory. The explicit local output preserves the ELF hash
across the converted build and supplies the native member. Matched clean builds
remain necessary because incremental object results alone supply no image claim.

## Mechanism comparison and bounded contribution

Source inspection uses `git show REV:PATH` and SHA-256 of the emitted bytes.
The [donor ledger](dpucp-2024-donor-ledger.json) binds DragonFlyBSD sources at
`805ca132aededa6ae4c8b3499a2d5c934d78fe56`. Both files also match revision
`94448f622a06cde90ba42d688e9cab2448ad9ab3` byte-for-byte.

| Source | Mechanism | Target treatment |
| --- | --- | --- |
| DragonFlyBSD `lib/libc/stdlib/abs.c` and `labs.c` | Typed conditional negation with public declarations | The standards-derived definitions preserve the representability domain and exact types |
| Coherent `mwc/romana/relic/d/lib/libc/gen/abs.c` and `labs.c` at `a00b86ea22c66625cf1838a71c06129f5eda2b49` | Conditional negation with old-style definitions and an explicit negative-minimum caveat | The domain caveat informs tests; modern prototypes replace historical language assumptions |
| Retained DiscoBSD `lib/libc/gen/abs.c` | Old-style int provider excluded from the maintained archive | Historical source remains intact; maintained selection belongs to stdlib |

Coherent's source hashes are
`acb784f4a446a86f26d2e4ad4195299dadbe36996ea8308f351c782d091d4fa6`
for `abs.c` and
`e30205849e1bcca9ccff3912a8b299adb1b9d73780bbd09a4528f06d70333f81`
for `labs.c`. These comparisons establish familiar prior art. The engineering
contribution is callable-provider closure across three archive paths and two
compiler paths; the arithmetic mechanism makes no novelty claim.

## Calibrated execution and resource boundaries

`absolute_value_test.c` executes 131,088 checks through volatile function pointers.
The matrix includes every signed 16-bit value, their representable positive
magnitudes, zero and native int/long boundary pairs. Expected values come from
nonnegative loop indices or explicit bounds. The oracle never negates an
unrepresentable minimum. LP64 host execution distinguishes the long range from
int; ILP32 and ARMv6-M exercise target widths.

`absolute_value.py` compiles strict C17 with warnings as errors and rejects
separate mutations that preserve negative inputs. Each mutation requires its
specific function's failure output and exit status. Compiler failures and
unrelated crashes supply no behavioral rejection. Native, ILP32, sanitizer
and Cortex-M0+ QEMU executions pass. The QEMU fixture uses newlib support;
its result establishes instruction execution for these functions.

The a.out contract takes public addresses and rejects removal of either member
from each native archive. The full member-omission matrix passes 24 controls.
The Smaller C fixture `t21_absolute.c` compiles, links and executes address-taking
with repeated public-header inclusion. `check-c17-headers-cross` requires empty
undefined-symbol sets for both providers. ARM GCC at `-Os -mcpu=cortex-m0plus
-mthumb -mfloat-abi=soft` produces eight text bytes per provider and zero data/BSS;
the generated instructions use registers only and return through `bx lr`.
This object observation supplies neither whole-image admission nor timing evidence.

Replay after the required logged build, with caller-selected `PYTHON`:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 check-libc-absolute-value check-c17-headers-cross
bmake MACHINE=rp2040 check-libc-aout-contracts
bmake -C usr.bin/smlrc MACHINE=rp2040 REQUIRE_QEMU=1 test
"$PYTHON" tests/libc_contracts/absolute_value.py . arm-none-eabi-gcc --arm
```

The [resource review](libc-absolute-value-resource-exception.md) records matched
clean distributions and exactly 240 additional native archive bytes with
unchanged root blocks. The owner approves the exact growth limits. Main includes
the qualified providers at `efe16f8b781c72a3a52ac7a75287c9c90214f5f2`
([PR #470](https://github.com/Oichkatzelesfrettschen/discobsd-2040-unofficial/pull/470)).
Existing string, lifecycle and context-sort approvals carry their exact limits;
they authorize no additional absolute-provider growth.

## Issue 8 requirement facets

The `abs` and `labs` rows close for the selected static RP2040 profile. The
closure combines the complete defined scalar argument domain, the source argument
below and the calibrated executable witnesses. The profile retains host as
the cataloged evidence class; the separately recorded ILP32, cross/native and
QEMU results keep their own execution conditions. The closure supplies no
whole-library, native C17 compiler or physical-board conformance claim.

| Facet | Authority and deciding evidence | Falsifier |
| --- | --- | --- |
| Required interface | Issue 8 `abs` and `labs` require representable integer magnitude and define no errors. Their `<stdlib.h>` declarations use `int` and `long`, respectively. | A required type, result or error rule differs from the inspected page. |
| Defined arithmetic | For an input x with representable magnitude, x >= 0 returns x. For x < 0, -x is representable by the domain premise, and the conditional evaluates that branch alone. The typed source performs exactly this operation. | A defined-domain input reaches overflowing arithmetic or returns a different magnitude. |
| State and ownership | Both functions own only their by-value automatic argument. They allocate nothing, mutate no shared state and call no other function. | Source, undefined imports or generated code introduce a resource, dependency or persistent state. |
| Public ABI | `stdlib.h`, strict C17 address-taking, LP64/ILP32 execution, ARMv6-M witnesses and the Smaller C `t21_absolute.c` fixture establish the selected types and callable symbols. | An exact declaration, address-taking or native call fails. |
| Build selection | Explicit stdlib selection preserves the separate ELF, full a.out and reduced-native members. Both native archives reject either missing member through its named unresolved symbol. | A legacy provider replaces the typed source or an omission still links. |
| Resource admission | The approved matched pair adds exactly 240 native archive bytes and leaves the other 157 measured surfaces unchanged. Later compositions retain their own approvals. | A new implementation or composition exceeds its separately approved vector. |
| Executable behavior | Each fixture runs 131,088 checks through volatile function pointers; independent negative-input controls reject both faulty providers. | A valid result differs, an oracle negates an unrepresentable minimum or a control fails for an unrelated reason. |

The canonical Issue 8 archive identity remains
`9a1237e4b7d7b779c6013911377bebe632a7b6765d242eee51859db06355e3df`.
Direct member inspection gives SHA-256
`ca40c2bac1d57aa1670da8f84c7f6dc2da98258ba3c003fb7e77398b74ddc159`
for `susv5-html/functions/abs.html` and
`b59c158f08473e34b27c242627a54e5a8a8550ef0fdd6ae9feafdccd87f05e52`
for `susv5-html/functions/labs.html`. The second page also describes `llabs`;
that distinct entity retains its independent ledger disposition. Magnitudes
outside the representable domain have undefined behavior under these pages
and receive neither a saturation promise nor an invented error contract.
Physical-board execution remains unrun and requires separate authorization.
