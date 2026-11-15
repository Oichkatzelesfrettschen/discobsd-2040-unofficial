# Native boolean type mechanism

Source pin: header candidate 7f4883ec, with main df48d76d. This investigation
supplies a follow-up specification; it implements no compiler behavior.

`usr.bin/smlrc/smlrc.c` lexes `_Bool` as `tok_Bool`, but `ParseBase` omits it. The same token
already names an internal truth conversion in exprval for logical operators.
`usr.bin/smlrc/cgthumb.c` computes `(x | -x) >> 31` in three Thumb-1
instructions using unsigned 32-bit modular arithmetic, including the INT_MIN
bit pattern. The frontend has a float comparison-with-zero path.
These mechanisms supply reusable normalization, not a complete declared type.

C17 section 6.3.1.2 governs conversion to `_Bool`.
The required invariant is B(x)=0 when x compares equal to zero and B(x)=1
otherwise. B must precede storage narrowing: B(256)=1 while trunc8(256)=0.
Floating conversion must compare before integer conversion: B(0.5)=1.
Signed zero converts to zero; nonzero values, infinities and NaNs convert to
one under the selected floating comparison contract. Pointer conversion tests
the complete pointer, not its low byte. Volatile expressions evaluate once.

The source closure includes ParseBase, GetDeclSize, GetDeclAlignment,
numericTypeCheck, promoteType, compatible-type traversal, cast processing,
formal argument conversion, assignment and compound assignment processing,
static/automatic initialization, and return conversion. Prefix/postfix
increment and decrement need explicit witnesses because their stored value
normalizes after arithmetic while the expression result follows C semantics.
Object size/alignment and parameter/result interchange require target ABI
fixtures with ARM GCC as an independent producer and consumer. Boolean
bit-fields require a field layout/load/store mechanism; the executable probes
below keep that prerequisite visible.

The deciding fixture uses constant and volatile input, 0/1/2/256/-1/INT_MIN,
float zero/fraction/NaN/infinity, null and non-null pointers, arrays and struct
fields, address-taking, initializers, assignment expressions, compound stores,
pre/post updates, function pointers, arguments, returns and integer promotion.
Acceptance requires generated Thumb execution under QEMU and cross-compiler
object linkage.
Negative controls include byte truncation in place of normalization, omitted
argument normalization, omitted return normalization and duplicate evaluation.
The public-header matrix changes its three unsupported inputs to accepted
only after the native type witnesses pass. The historical unsupported receipts
retain their original verdicts.

The ordinary enum fallback in stdbool.h cannot establish this type: enum
assignment preserves arbitrary representable integer values. Native support
must coordinate the preprocessor-visible compiler contract with stdbool.h so
an existing `_Bool` keyword does not meet a typedef of the same spelling.
No full C17 native claim follows from closing this single type.


## Initial conversion witness

At source `7f4883ec`, a standalone C17 fixture checks one-byte storage,
assignment, casts, typed arguments, return conversion, and prefix/postfix
increment/decrement for volatile integer inputs 0, 1, 2, 256, -1 and INT_MIN.
The host compiler passes the fixture. Replacing `_Bool` with unsigned char
produces the named assignment failure (exit 2). The native compiler builds
warning-clean, accepts a trivial integer program, and rejects the fixture
with `Unexpected token _Bool` (exit 1). This is a calibrated unsupported-type
result, not a native runtime pass. Floating, pointer, compound assignment,
aggregate initialization, and cross-compiler ABI witnesses remain open.

The fixture SHA-256 is `7dfb939621721372d68facad62146ea355e80898b34af13c2667947044ff230b`.
The host build uses `-std=c17 -pedantic-errors -Wall -Wextra -Werror`;
`-Wno-bool-operation` applies only to the intentional C17 increment/decrement
fixture. The native host build uses the maintained Thumb compiler configuration
and `-O1 -Wall -Wextra -Werror`. The replay fixture below fixes the input and
expected values independently of the compiler implementation.

```c
volatile int inputs[] = {0, 1, 2, 256, -1, (-2147483647-1)};
static _Bool convert(int value) { return value; }
static int argument(_Bool value) { return value; }
int main(void)
{
    unsigned i;
    _Bool object;
    if (sizeof(object) != 1) return 1;
    for (i = 0; i < 6; i++) {
        object = inputs[i];
        if (object != (i != 0)) return 2;
        if (convert(inputs[i]) != (i != 0)) return 3;
        if (argument(inputs[i]) != (i != 0)) return 4;
        if ((_Bool)inputs[i] != (i != 0)) return 5;
    }
    object = 0;
    if (object++ != 0 || object != 1) return 6;
    if (++object != 1 || object != 1) return 7;
    if (object-- != 1 || object != 0) return 8;
    if (--object != 1 || object != 1) return 9;
    return 0;
}
```

ARM GCC and Clang additionally accept compile-time checks for one-byte size,
one-byte alignment and two-byte storage for two boolean fields. Both reject
an unsigned-int substitution with the named boolean-size diagnostic. This
establishes the selected cross-compiler layout, separately from native ABI
interchange, which remains open.

## Conversion prerequisites exposed by source closure

`exprval` rejects floating operands in compound multiplication, division,
addition and subtraction before store conversion. `_Bool b = 0; b += 0.5f;`
therefore needs both floating compound evaluation and final truth normalization.
A boolean token alone does not close this valid input. The implementation
must evaluate the lvalue address once, load its old value, apply the usual
arithmetic conversion, perform the operation, normalize its result and store
one byte. An independent witness counts lvalue-address evaluation and checks
both the stored value and assignment-expression result.

`InitScalar` emits ordinary integer constants through `GenIntData` and accepts
relocatable address data only at machine-word width. A boolean initialized
from a permitted constant address therefore needs a deliberate scalar
normalization path before width-based data emission. Static `_Bool` values
initialized from integer 256, fractional floating constants, null pointers,
and addresses of ordinary objects/functions distinguish normalization from
truncation and relocation emission. Weak-symbol extensions require their own
contract if the native compiler admits them.

The current source search locates no explicit bit-field mechanism. That
negative is a lexical observation, not proof of absence. A declaration fixture
for both unsigned-int and `_Bool` bit-fields decides parser admission. Any
missing field layout/load/store mechanism remains a named prerequisite; the
boolean work does not relabel it as optional or completed.

Three executable probes refine the observation at source `7f4883ec`. The host
C17 compiler accepts and executes unsigned-int and boolean bit-field fixtures
and a float compound-add fixture. Smaller C rejects the unsigned bit-field at
`:` and the boolean bit-field at `_Bool`, both with exit 1. The float fixture
rejects with `Compound assignment not supported with floats`, also exit 1.
These exact diagnostics identify separate parser and expression prerequisites;
none supplies a completed native behavior claim.
