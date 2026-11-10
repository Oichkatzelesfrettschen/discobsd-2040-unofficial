#include <inttypes.h>

/* C17 7.8.2.1.  INTMAX_MIN has no positive counterpart and is undefined. */
intmax_t
imaxabs(intmax_t value)
{
	return value < 0 ? -value : value;
}
