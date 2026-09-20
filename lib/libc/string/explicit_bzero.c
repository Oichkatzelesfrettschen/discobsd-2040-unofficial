/*
 * Public domain.
 *
 * A volatile byte loop gives the small target an explicit compiler-visible
 * erasure boundary without importing memset or a linker barrier.
 */
#include <stddef.h>

void
explicit_bzero(void *buffer, size_t length)
{
	volatile unsigned char *byte = buffer;

	while (length-- != 0)
		*byte++ = 0;
}
