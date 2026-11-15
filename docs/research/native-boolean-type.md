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
bit-fields remain a separate compiler capability if the compiler lacks
bit-field syntax generally; that limitation stays visible.

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
