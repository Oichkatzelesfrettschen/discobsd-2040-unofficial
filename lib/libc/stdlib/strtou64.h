#ifndef _LIBC_STDLIB_STRTOU64_H_
#define _LIBC_STDLIB_STRTOU64_H_

/*
 * Result of __discobsd_strtou64: the magnitude of the subject sequence and
 * what the scan saw around it.  A magnitude above ULLONG_MAX sets overflow
 * and leaves magnitude at ULLONG_MAX.
 */
struct strtou64_result {
	unsigned long long	magnitude;
	unsigned char		negative;
	unsigned char		overflow;
};

struct strtou64_result __discobsd_strtou64(const char *, char **, int);

#endif /* !_LIBC_STDLIB_STRTOU64_H_ */
