#include <stddef.h>

int
timingsafe_bcmp(const void *first, const void *second, size_t length)
{
	const unsigned char *left = first;
	const unsigned char *right = second;

	while (length-- != 0) {
		if (*left++ != *right++)
			return 1;
	}
	return 0;
}
