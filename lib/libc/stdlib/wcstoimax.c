#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>

#include "strtou64.h"

/*
 * C17 7.8.2.4: the wide form of strtoimax.  intmax_t is long long, so the
 * range test is strtoll's: the magnitude fits at most INTMAX_MAX, or one
 * above it under a minus sign.
 */
intmax_t
wcstoimax(const wchar_t *string, wchar_t **end_pointer, int base)
{
	struct strtou64_result result;
	unsigned long long limit;

	result = __discobsd_wcstou64(string, end_pointer, base);
	limit = (unsigned long long)INTMAX_MAX + result.negative;
	if (result.overflow || result.magnitude > limit) {
		errno = ERANGE;
		return result.negative ? INTMAX_MIN : INTMAX_MAX;
	}
	if (!result.negative)
		return (intmax_t)result.magnitude;
	if (result.magnitude == limit)
		return INTMAX_MIN;
	return -(intmax_t)result.magnitude;
}
