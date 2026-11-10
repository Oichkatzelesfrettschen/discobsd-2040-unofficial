#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "strtou64.h"

#define	STRTOU64_SCAN	__discobsd_wcstou64
#define	STRTOU64_CHAR	wchar_t
#define	STRTOU64_UNIT	wchar_t
#include "strtou64_scan.h"

/*
 * C17 7.8.2.4: the wide form of strtoumax.  uintmax_t is unsigned long long,
 * so the result follows strtoull, including the negation of a value that
 * carried a minus sign.
 */
uintmax_t
wcstoumax(const wchar_t *string, wchar_t **end_pointer, int base)
{
	struct strtou64_result result;

	result = __discobsd_wcstou64(string, end_pointer, base);
	if (result.overflow) {
		errno = ERANGE;
		return UINTMAX_MAX;
	}
	return result.negative ? 0ULL - result.magnitude : result.magnitude;
}
