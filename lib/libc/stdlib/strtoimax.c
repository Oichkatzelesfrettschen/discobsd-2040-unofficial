#include <inttypes.h>
#include <stdlib.h>

/* C17 7.8.2.3: intmax_t is long long, so this is strtoll. */
intmax_t
strtoimax(const char *string, char **end_pointer, int base)
{
	return strtoll(string, end_pointer, base);
}
