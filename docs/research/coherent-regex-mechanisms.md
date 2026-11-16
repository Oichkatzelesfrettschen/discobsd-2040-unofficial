# Coherent regular-expression encoding and selection contracts

The pinned `gspu/Coherent` commit is
`a00b86ea22c66625cf1838a71c06129f5eda2b49`. Its
`mwc/romana/source/4.2.x/usr/src/misc/regexp.c` has SHA-256
`145883595e1d5cc63f119681f3a3273b532fb5800925bede0d956501439c886a`.
This record extends [the donor review](coherent-donor-review.md); DragonFlyBSD
retains the current comparison role. The experiments cover a decoded-offset
expression on contemporary hosts. Complete donor regex execution, historical
binary behavior and DiscoBSD regex implementation remain untested here.

## Offset encoding preserves both bytes

`regtail()` writes an offset as `(offset >> 8) & 0377` followed by
`offset & 0377`. `regnext()` reads that offset through this exact source macro:

```c
#define NEXT(p) (((*((p)+1)&0377)<<8) + *((p)+2)&0377)
```

C operator precedence groups the addition before the final bitwise AND. For
an eight-bit low byte, the expression computes the low byte of the sum and
discards every high-byte contribution. The required round-trip invariant is
`decode(encode(offset)) == offset` for admitted offsets. The compiler's
`regsize < 32767` admission still permits displacements above 255, so the
size guard does not establish the round-trip invariant.

The probe predicts agreement on offsets 0 through 255 and disagreement on
every higher two-byte value. GCC 16.2.1 20260810 native and ILP32 executions
and Clang 23.1.4 native execution each report 256 agreements and 65,280
disagreements. The correctly grouped expression agrees with the independent
`256 * high + low` oracle for all 65,536 byte pairs. The hosts use eight-bit
bytes and 32-bit `int`; these counts include values above the donor's
program-size admission and establish the expression's full two-byte domain,
not execution of that larger program domain. GCC's separate compilation of
the verbatim macro rejects it under `-Wall -Wextra -Werror` with
`-Werror=parentheses`. That compiler rejection provides additional diagnostic
evidence rather than a substitute for the arithmetic witness.

The executable fixture below explicitly parenthesizes the donor's existing
semantics. It preserves the original sum-before-mask rule and compares it
with the corrected grouping. It does not compile the complete donor file.

```c
#include <limits.h>
#include <stdio.h>

_Static_assert(CHAR_BIT == 8, "probe requires eight-bit bytes");

/* Parentheses expose the donor expression's C operator precedence. */
static unsigned int donor_decode(const unsigned char *bytes)
{
    return (((bytes[0] & 0377) << 8) + bytes[1]) & 0377;
}

static unsigned int corrected_decode(const unsigned char *bytes)
{
    return ((bytes[0] & 0377) << 8) + (bytes[1] & 0377);
}

int main(void)
{
    unsigned int donor_equal = 0, donor_different = 0;
    for (unsigned int high = 0; high < 256; high++) {
        for (unsigned int low = 0; low < 256; low++) {
            unsigned char bytes[2] = {(unsigned char)high, (unsigned char)low};
            unsigned int reference = 256u * high + low;
            if (corrected_decode(bytes) != reference)
                return 1;
            if (donor_decode(bytes) == reference)
                donor_equal++;
            else
                donor_different++;
        }
    }
    printf("donor matches=%u mismatches=%u; corrected matches=65536\n",
        donor_equal, donor_different);
    return donor_equal != 256 || donor_different != 65280;
}
```

With that fixture saved as `offset_probe.c`, the replay commands are:

```sh
set -eu
cc -std=c17 -pedantic-errors -Wall -Wextra -Werror offset_probe.c -o offset_probe
./offset_probe
cc -m32 -std=c17 -pedantic-errors -Wall -Wextra -Werror offset_probe.c -o offset_probe_ilp32
./offset_probe_ilp32
clang -std=c17 -pedantic-errors -Wall -Wextra -Werror offset_probe.c -o offset_probe_clang
./offset_probe_clang
```

The signedness of the original `char` changes intermediate low-byte
promotion but the final mask still removes the high byte on these 32-bit-int
implementations. The fixture exercises unsigned input bytes only; this final
signedness statement follows the modular arithmetic rather than a second
runtime population. A target implementation uses unsigned byte decoding and
checks each decoded displacement against the complete program extent before
forming a destination pointer. Correct byte composition alone supplies no
program-bounds validation.

## Alternative order and POSIX selection remain separate contracts

The donor's `regmatch()` BRANCH case returns as soon as the first alternative
succeeds. `regtry()` then publishes that attempt's captured extent. Source
tracing predicts that `a|aa` against `aa` selects the first `a`; POSIX ERE
leftmost-longest selection requires both bytes at that starting position.
This is a source-derived discriminating case, not a runtime observation.
Reordering alternatives only changes which example fails; a target engine
must resolve the required ordering over all successful paths and captures.

The donor's first-character, anchoring and required-literal filters remain
useful independent candidates. Their safe contract is one-sided rejection:
a filter may discard an input only when the matching engine cannot accept it.
A target experiment disables each filter while keeping the engine and inputs
identical, then compares match existence, complete extent and subexpressions.
Correlated executions of the same engine test filter equivalence; they do
not independently establish POSIX selection.

## Allocation and error ownership belong to the caller

The same directory's `local_misc.h` has SHA-256
`fc60596c9eafdcb26158c22d4856934e31280a22d1cb51e5e4bf1a2714478317`;
`alloc.c` has SHA-256
`bd522d06198ff15df8948a19b5ca495c208a7ca90fc9000766a37e0c007ca937`;
`regerror.c` has SHA-256
`47fdb6c26c45ddffbc9282e428a0e2e20b35b78e87c97a3e27ce640b25ade334`.
`alloc()` clears its allocation and calls `fatal()` on failure. The default
`regerror()` prints a diagnostic and terminates through `exit(1)` unless
`ERRAVAIL` selects an external error routine. Those utility-oriented policies
cannot directly provide a reusable POSIX library's caller-visible resource
errors. A target compiler returns an explicit error, preserves caller-owned
objects on failure and releases partially acquired storage.

The resulting investigation order is encoding round trips and program bounds,
explicit allocation/error ownership, per-call compile/execute state, normative
match/capture selection, and conservative rejection filters. Compact offsets
and two-pass compilation remain established mechanisms. Their combination
under DiscoBSD's storage and API contracts requires independent implementation,
calibrated witnesses and measured resources before adoption.
