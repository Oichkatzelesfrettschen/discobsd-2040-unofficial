# fptest -- RP2040 bootrom float verification

fptest checks that the AEABI single- and double-precision arithmetic routed
through the RP2040 bootrom (lib/libc/arm/gen/rom_float.c, wired by the
share/mk/sys.mk --wrap seam) returns the correct IEEE-754 result, bit for
bit, for a corpus of finite normal and signed-zero operands.

## Why bit-exact, not tolerance

An absolute-error check computes `fabs(got - want)`, whose own subtraction
runs the wrapped code under test -- a self-referential oracle. fptest instead
compares the raw result bits against a value computed independently on the
host under IEEE-754 round-to-nearest-even, which the bootrom reproduces
exactly for these operands (datasheet 2.8.3.2.1). Comparing bits also
distinguishes +0.0 from -0.0, so signed-zero regressions are caught.

## Building and running

    bmake MACHINE=rp2040

produces an a.out `fptest`. It is not in the shipped root manifest; copy it to
the board (rebuild an image that includes it, or transfer it over the
console) and run it. All cases passing prints:

    FPTEST OK

Any mismatch prints the case index, operation, operand bits, and the got vs
want bit patterns, and the program exits non-zero.

## Coverage and what is deferred

Covered: fadd/fsub/fmul/fdiv and dadd/dsub/dmul/ddiv over finite normal
operands, signed zero, and a rounding case per type. Deferred, because the
bootrom's declared contract flushes input and output subnormals to zero and
maps NaNs to infinities (datasheet 2.8.3.2.1) and the exact result bits for
those need on-device characterization before they can be an oracle:
subnormal, NaN, infinity, overflow, underflow, and halfway-rounding cases.
See sys/arch/rp2040/doc/research/float-libs.md.
