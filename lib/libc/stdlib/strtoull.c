#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "strtou64.h"

#define	STRTOU64_SCAN	__discobsd_strtou64
#define	STRTOU64_CHAR	char
#define	STRTOU64_UNIT	unsigned char
#include "strtou64_scan.h"

/*
 * C17 7.22.1.4p5 negates a value that carried a minus sign in the return
 * type, so "-1" is ULLONG_MAX with errno untouched.
 */
unsigned long long
strtoull(const char *string, char **end_pointer, int base)
{
	struct strtou64_result result;

	result = __discobsd_strtou64(string, end_pointer, base);
	if (result.overflow) {
		errno = ERANGE;
		return ULLONG_MAX;
	}
	return result.negative ? 0ULL - result.magnitude : result.magnitude;
}
