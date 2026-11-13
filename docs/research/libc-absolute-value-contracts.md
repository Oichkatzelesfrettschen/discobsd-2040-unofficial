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

The compatibility rows remain partial with host evidence. The
[resource review](libc-absolute-value-resource-exception.md) records matched
clean distributions and exactly 240 additional native archive bytes with
unchanged root blocks. Resource approval, aggregate qualification and reviewed
integration remain open. Existing string, lifecycle and context-sort approvals
carry their exact limits; they authorize no additional absolute-provider growth.
Physical-board execution remains unrun and requires separate authorization.
