/*
 * Exit-on-failure wrapper for the libc reallocarray(3) implementation.
 */
#include <stdlib.h>

#include "compat.h"

void *reallocarray(void *, size_t, size_t);

void *
ereallocarray(void *pointer, size_t element_count, size_t element_size)
{
	void *resized_pointer;

	resized_pointer = reallocarray(pointer, element_count, element_size);
	if (resized_pointer == NULL)
		eprintf("reallocarray: out of memory\n");
	return resized_pointer;
}
