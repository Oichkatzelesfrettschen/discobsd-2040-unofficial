Smaller C is a simple single-pass C compiler.  The maintained DiscoBSD
version emits ARMv6-M Thumb-1 assembly for GNU as.  The original non-ARM
back ends live under legacy/non-arm/ and do not participate in this build.

The compiler is capable of compiling itself.

The compiler uses the repository assembler, linker, headers, and C library.
README.rp2040.md documents the maintained language subset, ABI, and tests.

Normative and other useful documents on C:
C99 + TC1 + TC2 + TC3, WG14 N1256:
  http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf
Rationale for C99:
  http://www.open-std.org/jtc1/sc22/wg14/www/docs/C99RationaleV5.10.pdf
The New C Standard: An Economic and Cultural Commentary:
  http://www.knosof.co.uk/cbook/cbook.html
