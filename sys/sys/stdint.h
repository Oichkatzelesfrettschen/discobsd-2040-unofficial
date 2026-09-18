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

#define INT8_MAX            0x7F
#define UINT8_MAX           0xFFU

#define INT16_MAX           0x7FFF
#define UINT16_MAX          0xFFFFU

#define INT32_MAX           0x7FFFFFFF
#define UINT32_MAX          0xFFFFFFFFU

/* size_t is unsigned int on every port (sys/types.h), so its ceiling is
 * UINT32_MAX; C99 7.18.3 puts the name here, and calloc() divides by it. */
#define SIZE_MAX            UINT32_MAX

#define UINT32_C(x)         (x##U)
#define UINT64_C(x)         (x##ULL)

#endif /* _STDINT_H */
