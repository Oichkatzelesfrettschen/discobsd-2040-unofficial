#ifndef _LIBC_STDLIB_STRTOU64_H_
#define _LIBC_STDLIB_STRTOU64_H_

#include <stddef.h>

/*
 * Result of __discobsd_strtou64 and __discobsd_wcstou64: the magnitude of
 * the subject sequence and what the scan saw around it.  A magnitude above ULLONG_MAX sets overflow
 * and leaves magnitude at ULLONG_MAX.
 */
struct strtou64_result {
	unsigned long long	magnitude;
	unsigned char		negative;
	unsigned char		overflow;
};

struct strtou64_result __discobsd_strtou64(const char *, char **, int);
struct strtou64_result __discobsd_wcstou64(const wchar_t *, wchar_t **, int);

#endif /* !_LIBC_STDLIB_STRTOU64_H_ */
