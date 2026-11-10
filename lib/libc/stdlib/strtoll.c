#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "strtou64.h"

/*
 * The magnitude is in range when it is at most LLONG_MAX, or at most one
 * above it under a minus sign, since LLONG_MIN has no positive counterpart.
 */
long long
strtoll(const char *string, char **end_pointer, int base)
{
	struct strtou64_result result;
	unsigned long long limit;

	result = __discobsd_strtou64(string, end_pointer, base);
	limit = (unsigned long long)LLONG_MAX + result.negative;
	if (result.overflow || result.magnitude > limit) {
		errno = ERANGE;
		return result.negative ? LLONG_MIN : LLONG_MAX;
	}
	if (!result.negative)
		return (long long)result.magnitude;
	if (result.magnitude == limit)
		return LLONG_MIN;
	return -(long long)result.magnitude;
}
