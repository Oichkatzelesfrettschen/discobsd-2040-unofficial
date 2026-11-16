# Callback registration block and arena accounting

The DiscoBSD source at `62b89c3` supplies an eight-byte callback node on
ILP32. `lib/libc/gen/malloc.c` computes `(nbytes + WORD + WORD - 1) / WORD`
with a four-byte `union store`, so each dynamic node occupies twelve bytes.
The arena's terminal header and 1,024-byte break alignment contribute separate
storage. The [Coherent lifecycle comparison](coherent-boundary-lifecycle-witnesses.md)
retains the fixed-array alternative and callback-order contract.

## Source identity and modeled execution

| Source | SHA-256 |
| --- | --- |
| `lib/libc/gen/malloc.c` | `f50ca037fad608689c2377ac83a743416b93409194d3852488273b98a96093e1` |
| `lib/libc/gen/malloc_store.h` | `7ce9a643baaf8fbfc621606bfda4e08044bf521465ebef0ca0630a5022819453` |
| `lib/libc/stdio/atexit.c` | `3461d72cf880f1326d5619237ec466593f284b8ccda5446ec0483f2294a848d2` |
| `tests/libc_contracts/malloc_arena.c` | `0652ca19d8befd7f08f5fb3b2699b20917ee6be3fc465c26aad20b40f12670f2` |

The experiment uses GCC 16.2.1 20260810, Linux x86-64, glibc 2.44 and `-m32`.
The production allocator uses GNU17; the registration source and fixture use
strict C17. Every compile uses `-Wall -Wextra -Werror`. A modeled `sbrk(int)`
owns an 8,192-byte host allocation and records break growth. Each case starts
in a fresh process with a cold target allocator. The arena base aligns to
2,048 bytes and a tested offset establishes the phase within the allocator's
1,024-byte boundary. This is host execution of renamed production sources;
physical-board allocation, complete process peaks and exit callbacks remain
outside the experiment.

For `n` registrations, the static first node leaves `k = max(n - 1, 0)`
dynamic nodes. The prediction for their occupied blocks is `12k` bytes.
For `k > 0` and initial offset `o`, the predicted break growth is
`1024 * ceil((o + 12k + 4) / 1024) - o`; the extra four bytes retain the
terminal header. Zero dynamic nodes require zero break growth. The experiment
records these predictions before running the matrix.

| Registrations | Dynamic node blocks | Observed break-growth range | Starting offsets |
| ---: | ---: | ---: | ---: |
| 0 | 0 bytes | 0 bytes | 256 |
| 1 | 0 bytes | 0 bytes | 256 |
| 4 | 36 bytes | 40–1,060 bytes | 256 |
| 32 | 372 bytes | 376–1,396 bytes | 256 |

Every four-byte-aligned offset from 0 through 1,020 passes for each row. The
matrix covers 1,024 cases. An instrumented list walk counts the actual nodes
and reads their allocator headers; the break counter observes the independent
arena-commitment surface. Existing free arena capacity, prior fragmentation,
interleaved allocations, and registration counts outside the four table rows
remain outside these observed ranges.

## Calibration separates alignment from growth policy

A control increases the node request by four bytes and rejects with the
fixture's block-extent exit code 4. A second control changes the allocator
block size from 1,024 to 2,048 bytes and rejects with the growth-mismatch
exit code 1 for four registrations at offset zero. The latter control records
2,048 break bytes against the original allocator's 1,024 bytes.

The first calibration attempt aligns the fixture only to 1,024 bytes. That
fixture sometimes places the original break halfway through a 2,048-byte
boundary and the larger-granule control then also extends by 1,024 bytes.
The controlled 2,048-byte base removes that ambiguity. A repeat of all
1,024 original cases still passes, while the larger-granule control rejects.
The initial ambiguous result supplies no rejection evidence.

## Storage policy follows caller and archive ownership

For a consumer selecting both callback registration and exit, the registry
head and static node contribute twelve bytes before dynamic nodes. The
modeled node-and-registry total is therefore `12n` for `n >= 1`, excluding
allocator state and unused arena space. Coherent's fixed array/count/cursor
model occupies 136 bytes. Those narrow totals cross between eleven and
twelve registrations. The cold break measurements show why that crossover
alone cannot choose a representation: a small number of registrations can
commit substantially more arena space than their live blocks occupy.

The existing arena can absorb later callbacks without additional break growth,
and its unoccupied capacity can serve other allocations. Count that capacity
once. A fixed array supplies predictable admission and footprint but reserves
its selected storage independently of registration count. Static archive
selection and shared-libc per-process object reservation impose different
lifetimes on that fixed storage. The deciding quick-exit comparison therefore
includes zero-use shared-state reservation, cold first registration, warmed
heap allocation, 32 registrations and failed spill allocation. Whole-consumer
code, BSS, heap commitment, callback peak stack and swap occupancy remain
separate resource measurements; these host observations choose no new ABI.

## Replay fixture

Save this fixture as `callback_probe.c` in a caller-owned build directory.
Its SHA-256 is `ce82a26be09ce9fd53f77d80528af5053d41fc53ec736fba258ddbbfa3a40f95`.

```c
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "atexit.h"
#include "malloc_store.h"

_Static_assert(sizeof(void *) == 4 && sizeof(union store) == 4,
    "allocation probe requires ILP32");

struct atexit *db_atexit_head;
int db_atexit(void (*)(void));
static unsigned char *arena_start;
static unsigned char *arena_break;
static unsigned char *arena_limit;

void *test_sbrk(int increment)
{
    unsigned char *before = arena_break;
    if (increment < 0 || (size_t)increment > (size_t)(arena_limit-arena_break)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    arena_break += increment;
    return before;
}

static void callback(void) { }

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    unsigned int count = (unsigned int)strtoul(argv[1], NULL, 10);
    unsigned int offset = (unsigned int)strtoul(argv[2], NULL, 10);
    if (count > 32 || offset >= 1024 || offset % 4 != 0)
        return 2;
    unsigned char *storage = malloc(8192);
    if (storage == NULL)
        return 2;
    uintptr_t aligned = ((uintptr_t)storage + 2047u) & ~(uintptr_t)2047u;
    arena_start = (unsigned char *)aligned + offset;
    arena_break = arena_start;
    arena_limit = storage + 8192;
    for (unsigned int index=0; index<count; index++)
        if (db_atexit(callback) != 0)
            return 3;
    unsigned int nodes=0, dynamic=0;
    size_t charged=0;
    for (struct atexit *node=db_atexit_head; node; node=node->next) {
        nodes++;
        if ((uintptr_t)node >= (uintptr_t)arena_start &&
            (uintptr_t)node < (uintptr_t)arena_break) {
            union store *header=(union store *)node-1;
            size_t extent=(size_t)((uintptr_t)clearbusy(header->ptr)-(uintptr_t)header);
            if (extent != 12)
                return 4;
            charged+=extent;
            dynamic++;
        }
    }
    size_t expected_dynamic = count > 0 ? count-1 : 0;
    size_t expected_growth = expected_dynamic == 0 ? 0 :
        ((offset+12*expected_dynamic+4+1023)/1024)*1024-offset;
    size_t growth=(size_t)(arena_break-arena_start);
    printf("%u %u %zu %zu\n", count, offset, charged, growth);
    if (nodes != count || dynamic != expected_dynamic ||
        charged != 12*expected_dynamic || growth != expected_growth)
        return 1;
    free(storage);
    return 0;
}
```

From the repository root, with the fixture path selected as `CALLBACK_PROBE`
and its output directory selected as `CALLBACK_OUTPUT`, compile the sources:

```sh
set -eu
: "${CALLBACK_PROBE:?select callback_probe.c}"
: "${CALLBACK_OUTPUT:?select an existing build directory}"
cc -m32 -std=gnu17 -Wall -Wextra -Werror \
    -Dmalloc=db_malloc -Dfree=db_free -Drealloc=db_realloc \
    -Dcalloc=db_calloc -Dcfree=db_cfree \
    -c tests/libc_contracts/malloc_arena.c -o "$CALLBACK_OUTPUT/malloc.o"
cc -m32 -std=c17 -pedantic-errors -Wall -Wextra -Werror \
    -Datexit=db_atexit -D__atexit=db_atexit_head -Dmalloc=db_malloc \
    -c lib/libc/stdio/atexit.c -o "$CALLBACK_OUTPUT/atexit.o"
cc -m32 -std=c17 -pedantic-errors -Wall -Wextra -Werror \
    -I lib/libc/stdio -I lib/libc/gen "$CALLBACK_PROBE" \
    "$CALLBACK_OUTPUT/malloc.o" "$CALLBACK_OUTPUT/atexit.o" \
    -o "$CALLBACK_OUTPUT/callback_probe"
for registrations in 0 1 4 32; do
    offset=0
    while [ "$offset" -lt 1024 ]; do
        "$CALLBACK_OUTPUT/callback_probe" "$registrations" "$offset"
        offset=$((offset + 4))
    done
done
```

The node control replaces the unique `malloc(sizeof(struct atexit))` expression
in a build-directory copy of `atexit.c` with
`malloc(sizeof(struct atexit) + 4)`. The granule control replaces the unique
`#define BLOCK 1024` definition, retaining its source whitespace, with 2048
in a build-directory copy of `malloc.c`, and points a copied allocator wrapper
at that copy. Each control uses the same compile flags and fixture. Run each
at `4 0` and require the exact exit codes above; compiler failure or a different
runtime error fails calibration.
