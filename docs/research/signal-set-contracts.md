# Signal-set provider and header contracts

## Baseline and deciding experiment

The work branches from owned main `f602046341801b77feff61a2c3549c119ffb8d37`.
`lib/libc/gen/sigsetops.c` supplies five callable set operations and belongs
to `lib/libc/gen/Makefile`. `sys/sys/signal.h` supplies corresponding macros,
but its public declarations omit all five callable signatures. `sigset_t`
is unsigned long; the maintained ILP32 target represents it in 32 bits.

The callable `sigismember` tests `*set & ~sigmask(signo)`, which asks whether
any other bit is present. The macro tests the requested bit. Source search
uses `rg -n 'sigismember|sigsetops'` over the header, provider, and build files.
The governing membership invariant is `(set & (1U << (signo - 1))) != 0`
for admitted signal numbers 1 through 31. The callable operation requires a
const-qualified set pointer and the same answer as the public call form.

A bounded host GNU17/UBSan probe includes the actual provider with an explicit
`uint32_t` signal-set shim. It enumerates all 1,024 masks in the low ten bits
and all 31 valid signal numbers. The baseline reports 26,603 mismatches in
31,744 calls with exit status 1 and no sanitizer diagnostic. The shim models
the target representation; it does not qualify target headers, archive
membership, kernel behavior, or physical hardware. The provider SHA-256 is
`9359368cfa3db344eef72833927561b79f8ab4cb18d81d8e143fcbdf88f1cf26`.

The predicted repair reduces membership mismatches to zero on this exact
domain. Additional witnesses require empty/full sets, each individual bit,
all bits, argument single evaluation, callable/macro parity, exact signatures,
and invalid-signal errors before any shift. The current provider and macros
lack invalid-number admission. A valid-domain probe establishes no behavior
for those invalid inputs.

## Donor mechanisms

The inspected local DragonFlyBSD revision is
`94448f622a06cde90ba42d688e9cab2448ad9ab3`; its
`lib/libc/gen/sigsetops.c` SHA-256 is
`582715e1a1eb109f89db743c69143b2d385dde6a6d4db60a5061cd330ec803bc`.
It validates the signal number before deriving a word and bit in its multiword
set, returns `-1` with `EINVAL` for invalid numbers, and tests the selected bit.
The transferable invariant is admission before shifting; its multiword
representation does not replace the port's scalar ABI.

The inspected local OpenBSD revision is
`8849c5d46b9b299f5d887033706f1ddd60f8f966`; the same relative file has SHA-256
`347b32b34b233938c932435b74be0fbab31be4d361ff13afbf0cf685f7594883`.
Its scalar provider validates `0 < signo < NSIG` and tests the requested bit.
Both donors carry shared Berkeley ancestry; they supply alternative current
mechanisms at these retained local revisions, not independent proofs or a
claim that these revisions match live upstream.

## Implementation and qualification boundary

The repair preserves the scalar public representation and the kernel ABI.
The header and provider require one consistent validation policy, exact
prototypes, const membership input, and single evaluation. Empty and full
operations preserve their established bit representations. Invalid add/delete
operations leave the set unchanged and report `EINVAL`; invalid membership
reports `-1` with `EINVAL`. No invalid number reaches a shift.

The implementation choice must retain native Smaller C header acceptance.
Replacing macros with callable operations trades local expressions for archive
dependencies and therefore requires caller closure and matched resource
measurement. Inline alternatives require explicit native-compiler evidence;
they do not receive assumed acceptance. Remaining qualification includes
strict C17 mechanisms, GNU17 production, target header compilation, native
archive providers, host/ILP32/Cortex-M0 execution, calibrated mutants, clean
shipped-resource comparison, and the profile's exception process for growth.

The membership experiment establishes a defect and a falsifier. Implementation,
complete public-header contracts, resource admission, and integration remain
open. Physical-board execution retains its separate authorization boundary.
