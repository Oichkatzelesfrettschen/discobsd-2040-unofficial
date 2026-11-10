#include <inttypes.h>
#include <stdlib.h>

/* C17 7.8.2.3: uintmax_t is unsigned long long, so this is strtoull. */
uintmax_t
strtoumax(const char *string, char **end_pointer, int base)
{
	return strtoull(string, end_pointer, base);
}
