#include <stdlib.h>

long
labs(long value)
{
	return value < 0 ? -value : value;
}
