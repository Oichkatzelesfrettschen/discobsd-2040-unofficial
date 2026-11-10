#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <signal.h>

/* Every limit is an #if constant: C17 7.20.2p2. */
#if INT8_MIN != -128 || INT8_MAX != 127 || UINT8_MAX != 255
#error "8-bit limits"
#endif
#if INT16_MIN != -32768 || INT16_MAX != 32767 || UINT16_MAX != 65535
#error "16-bit limits"
#endif
#if INT32_MIN + INT32_MAX != -1 || UINT32_MAX != 4294967295
#error "32-bit limits"
#endif
#if INT64_MIN + INT64_MAX != -1 || UINT64_MAX != 18446744073709551615U
#error "64-bit limits"
#endif
#if INTMAX_MIN != LLONG_MIN || INTMAX_MAX != LLONG_MAX || \
    UINTMAX_MAX != ULLONG_MAX
#error "intmax_t limits are the long long limits"
#endif
#if INT8_C(-5) != -5 || UINT16_C(65535) != 65535 || \
    INT64_C(9223372036854775807) != INT64_MAX || \
    UINTMAX_C(18446744073709551615) != UINTMAX_MAX
#error "constant macros"
#endif

/* A limit has the type its integer type promotes to. */
#define	SAME(expression, type) \
	_Generic((expression), type: 1, default: 0)
_Static_assert(SAME(UINT8_MAX, int) && SAME(UINT16_MAX, int),
    "8- and 16-bit unsigned maxima promote to int");
_Static_assert(SAME(INT8_MIN, int) && SAME(INT16_MIN, int),
    "8- and 16-bit minima are int");
_Static_assert(SAME(UINT32_MAX, uint32_t) && SAME(INT32_MIN, int32_t),
    "32-bit limits");
_Static_assert(SAME(UINT64_MAX, uint64_t) && SAME(INT64_MIN, int64_t) &&
    SAME(INT64_MAX, int64_t), "64-bit limits");
_Static_assert(SAME(INTPTR_MAX, intptr_t) && SAME(INTPTR_MIN, intptr_t) &&
    SAME(UINTPTR_MAX, uintptr_t), "pointer limits");
_Static_assert(SAME(INTMAX_MAX, intmax_t) && SAME(UINTMAX_MAX, uintmax_t),
    "greatest-width limits");
_Static_assert(SAME(PTRDIFF_MAX, ptrdiff_t) && SAME(SIZE_MAX, size_t) &&
    SAME(SIG_ATOMIC_MAX, sig_atomic_t), "limits of other integer types");
_Static_assert(SAME(INT64_C(1), int_least64_t) &&
    SAME(UINT64_C(1), uint_least64_t) && SAME(INTMAX_C(1), intmax_t) &&
    SAME(UINTMAX_C(1), uintmax_t) && SAME(UINT32_C(1), uint_least32_t) &&
    SAME(INT8_C(1), int) && SAME(UINT16_C(1), int),
    "constant macros have the promoted least-width type");

/* Every limit is the true extreme of its type. */
#define	SIGNED_LIMITS(type, minimum, maximum) \
	_Static_assert((type)(maximum) == (maximum) && \
	    (type)(minimum) == (minimum) && \
	    (maximum) == (type)(((uintmax_t)1 << (sizeof(type) * CHAR_BIT - 1)) - 1) && \
	    (minimum) == -(maximum) - 1, #type " limits")
#define	UNSIGNED_LIMIT(type, maximum) \
	_Static_assert((maximum) == (type)-1 && (type)-1 > 0, #type " limit")

SIGNED_LIMITS(int8_t, INT8_MIN, INT8_MAX);
SIGNED_LIMITS(int16_t, INT16_MIN, INT16_MAX);
SIGNED_LIMITS(int32_t, INT32_MIN, INT32_MAX);
SIGNED_LIMITS(int64_t, INT64_MIN, INT64_MAX);
SIGNED_LIMITS(int_least8_t, INT_LEAST8_MIN, INT_LEAST8_MAX);
SIGNED_LIMITS(int_least16_t, INT_LEAST16_MIN, INT_LEAST16_MAX);
SIGNED_LIMITS(int_least32_t, INT_LEAST32_MIN, INT_LEAST32_MAX);
SIGNED_LIMITS(int_least64_t, INT_LEAST64_MIN, INT_LEAST64_MAX);
SIGNED_LIMITS(int_fast8_t, INT_FAST8_MIN, INT_FAST8_MAX);
SIGNED_LIMITS(int_fast16_t, INT_FAST16_MIN, INT_FAST16_MAX);
SIGNED_LIMITS(int_fast32_t, INT_FAST32_MIN, INT_FAST32_MAX);
SIGNED_LIMITS(int_fast64_t, INT_FAST64_MIN, INT_FAST64_MAX);
SIGNED_LIMITS(intptr_t, INTPTR_MIN, INTPTR_MAX);
SIGNED_LIMITS(intmax_t, INTMAX_MIN, INTMAX_MAX);
SIGNED_LIMITS(ptrdiff_t, PTRDIFF_MIN, PTRDIFF_MAX);
SIGNED_LIMITS(sig_atomic_t, SIG_ATOMIC_MIN, SIG_ATOMIC_MAX);
UNSIGNED_LIMIT(uint8_t, UINT8_MAX);
UNSIGNED_LIMIT(uint16_t, UINT16_MAX);
UNSIGNED_LIMIT(uint32_t, UINT32_MAX);
UNSIGNED_LIMIT(uint64_t, UINT64_MAX);
UNSIGNED_LIMIT(uint_least8_t, UINT_LEAST8_MAX);
UNSIGNED_LIMIT(uint_least16_t, UINT_LEAST16_MAX);
UNSIGNED_LIMIT(uint_least32_t, UINT_LEAST32_MAX);
UNSIGNED_LIMIT(uint_least64_t, UINT_LEAST64_MAX);
UNSIGNED_LIMIT(uint_fast8_t, UINT_FAST8_MAX);
UNSIGNED_LIMIT(uint_fast16_t, UINT_FAST16_MAX);
UNSIGNED_LIMIT(uint_fast32_t, UINT_FAST32_MAX);
UNSIGNED_LIMIT(uint_fast64_t, UINT_FAST64_MAX);
UNSIGNED_LIMIT(uintptr_t, UINTPTR_MAX);
UNSIGNED_LIMIT(uintmax_t, UINTMAX_MAX);
UNSIGNED_LIMIT(size_t, SIZE_MAX);

/* Width ordering of the least and fast types: C17 7.20.1.2, 7.20.1.3. */
_Static_assert(sizeof(int_least8_t) == 1 && sizeof(int_least16_t) == 2 &&
    sizeof(int_least32_t) == 4 && sizeof(int_least64_t) == 8,
    "least types are the exact-width types");
_Static_assert(sizeof(int_fast8_t) >= 1 && sizeof(int_fast16_t) >= 2 &&
    sizeof(int_fast32_t) >= 4 && sizeof(int_fast64_t) >= 8,
    "fast types hold their width");
_Static_assert(sizeof(intmax_t) >= sizeof(long long) &&
    sizeof(uintmax_t) >= sizeof(unsigned long long),
    "intmax_t holds every integer type");
_Static_assert(sizeof(intptr_t) == sizeof(void *), "intptr_t holds a pointer");

/* The compiler agrees about the types it assigns to literals and to %j. */
_Static_assert(SAME((intmax_t)0, __INTMAX_TYPE__) &&
    SAME((uintmax_t)0, __UINTMAX_TYPE__), "intmax_t is the compiler's");
_Static_assert(WCHAR_MAX == __WCHAR_MAX__ && WINT_MAX == __WINT_MAX__ &&
    WCHAR_MIN == __WCHAR_MIN__ && WINT_MIN == __WINT_MIN__,
    "wide limits describe the compiler's types");
