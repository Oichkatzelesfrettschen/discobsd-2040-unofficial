#include <limits.h>

#if LLONG_MAX != 9223372036854775807 || ULLONG_MAX != 18446744073709551615U
#error "C17 5.2.4.2.1 long long limits are #if constants of the stated value"
#endif
#if LLONG_MIN + LLONG_MAX != -1
#error "LLONG_MIN is one below the negated LLONG_MAX"
#endif

_Static_assert(_Generic(LLONG_MAX, long long: 1, default: 0),
    "LLONG_MAX has type long long");
_Static_assert(_Generic(LLONG_MIN, long long: 1, default: 0),
    "LLONG_MIN has type long long");
_Static_assert(_Generic(ULLONG_MAX, unsigned long long: 1, default: 0),
    "ULLONG_MAX has type unsigned long long");
_Static_assert(ULLONG_MAX == (unsigned long long)-1,
    "ULLONG_MAX is the all-ones value");
_Static_assert(sizeof(long long) * CHAR_BIT == 64,
    "long long is the 64-bit type the limits describe");
