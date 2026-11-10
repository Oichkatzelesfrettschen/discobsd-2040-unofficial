#include <stdlib.h>

/* C17 7.22.1.2p2: atoll is strtoll in base 10 without an end pointer. */
long long
atoll(const char *string)
{
	return strtoll(string, NULL, 10);
}
