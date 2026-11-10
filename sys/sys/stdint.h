#ifndef _STDINT_H
#define _STDINT_H

typedef signed char         int8_t;
typedef short int           int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

typedef unsigned char       uint8_t;
typedef unsigned short int  uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef long                intptr_t;
typedef unsigned long       uintptr_t;

/*
 * C17 7.20.1.2 and 7.20.1.3.  The exact-width types are already the
 * smallest of their width, and int is the fastest type for every width it
 * holds.
 */
typedef int8_t              int_least8_t;
typedef int16_t             int_least16_t;
typedef int32_t             int_least32_t;
typedef int64_t             int_least64_t;
typedef uint8_t             uint_least8_t;
typedef uint16_t            uint_least16_t;
typedef uint32_t            uint_least32_t;
typedef uint64_t            uint_least64_t;

typedef int                 int_fast8_t;
typedef int                 int_fast16_t;
typedef int                 int_fast32_t;
typedef int64_t             int_fast64_t;
typedef unsigned int        uint_fast8_t;
typedef unsigned int        uint_fast16_t;
typedef unsigned int        uint_fast32_t;
typedef uint64_t            uint_fast64_t;

/* C17 7.20.1.5: wide enough for every integer type, so long long. */
typedef long long           intmax_t;
typedef unsigned long long  uintmax_t;

/*
 * C17 7.20.2p2: each limit has the type its integer type promotes to and
 * is usable in #if.  The 8- and 16-bit maxima are therefore int.
 */
#define INT8_MIN            (-0x7F - 1)
#define INT8_MAX            0x7F
#define UINT8_MAX           0xFF

#define INT16_MIN           (-0x7FFF - 1)
#define INT16_MAX           0x7FFF
#define UINT16_MAX          0xFFFF

#define INT32_MIN           (-0x7FFFFFFF - 1)
#define INT32_MAX           0x7FFFFFFF
#define UINT32_MAX          0xFFFFFFFFU

#define INT64_MIN           (-0x7FFFFFFFFFFFFFFFLL - 1)
#define INT64_MAX           0x7FFFFFFFFFFFFFFFLL
#define UINT64_MAX          0xFFFFFFFFFFFFFFFFULL

#define INT_LEAST8_MIN      INT8_MIN
#define INT_LEAST8_MAX      INT8_MAX
#define UINT_LEAST8_MAX     UINT8_MAX
#define INT_LEAST16_MIN     INT16_MIN
#define INT_LEAST16_MAX     INT16_MAX
#define UINT_LEAST16_MAX    UINT16_MAX
#define INT_LEAST32_MIN     INT32_MIN
#define INT_LEAST32_MAX     INT32_MAX
#define UINT_LEAST32_MAX    UINT32_MAX
#define INT_LEAST64_MIN     INT64_MIN
#define INT_LEAST64_MAX     INT64_MAX
#define UINT_LEAST64_MAX    UINT64_MAX

#define INT_FAST8_MIN       INT32_MIN
#define INT_FAST8_MAX       INT32_MAX
#define UINT_FAST8_MAX      UINT32_MAX
#define INT_FAST16_MIN      INT32_MIN
#define INT_FAST16_MAX      INT32_MAX
#define UINT_FAST16_MAX     UINT32_MAX
#define INT_FAST32_MIN      INT32_MIN
#define INT_FAST32_MAX      INT32_MAX
#define UINT_FAST32_MAX     UINT32_MAX
#define INT_FAST64_MIN      INT64_MIN
#define INT_FAST64_MAX      INT64_MAX
#define UINT_FAST64_MAX     UINT64_MAX

#define INTPTR_MIN          (-0x7FFFFFFFL - 1)
#define INTPTR_MAX          0x7FFFFFFFL
#define UINTPTR_MAX         0xFFFFFFFFUL

#define INTMAX_MIN          INT64_MIN
#define INTMAX_MAX          INT64_MAX
#define UINTMAX_MAX         UINT64_MAX

/* ptrdiff_t (stddef.h) and sig_atomic_t (sys/signal.h) are both int. */
#define PTRDIFF_MIN         INT32_MIN
#define PTRDIFF_MAX         INT32_MAX
#define SIG_ATOMIC_MIN      INT32_MIN
#define SIG_ATOMIC_MAX      INT32_MAX

/*
 * The library declares no wide character type, so these four limits
 * describe the compiler's wchar_t and wint_t, which is where a wide
 * literal gets its type.
 */
#ifdef __WCHAR_MAX__
#define WCHAR_MIN           __WCHAR_MIN__
#define WCHAR_MAX           __WCHAR_MAX__
#endif
#ifdef __WINT_MAX__
#define WINT_MIN            __WINT_MIN__
#define WINT_MAX            __WINT_MAX__
#endif

/* size_t is unsigned int on every port (sys/types.h), so its ceiling is
 * UINT32_MAX; C99 7.18.3 puts the name here, and calloc() divides by it. */
#define SIZE_MAX            UINT32_MAX

/*
 * C17 7.20.4: a constant of the type the least-width type promotes to, so
 * the 8- and 16-bit forms add no suffix.
 */
#define INT8_C(x)           (x)
#define INT16_C(x)          (x)
#define INT32_C(x)          (x)
#define INT64_C(x)          (x##LL)
#define UINT8_C(x)          (x)
#define UINT16_C(x)         (x)
#define UINT32_C(x)         (x##U)
#define UINT64_C(x)         (x##ULL)
#define INTMAX_C(x)         (x##LL)
#define UINTMAX_C(x)        (x##ULL)

#endif /* _STDINT_H */
