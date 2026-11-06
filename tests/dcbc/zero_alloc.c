/*
 * The tree's allocator (lib/libc/gen/malloc.c) answers a zero-byte request
 * with NULL, and realloc(p, 0) frees p and returns NULL; the host C libraries
 * answer with a unique pointer. A host build of dc or bc linked with
 * -Wl,--wrap=malloc,--wrap=realloc,--wrap=reallocarray takes these
 * definitions, so a caller that treats NULL as an allocation failure (dc's
 * bmalloc and breallocarray in mem.c) fails here as it does on the target.
 * peak_alloc.c applies the same rule to its accounting wrappers.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);

void *
__wrap_malloc(size_t n)
{
	if (n == 0)
		return NULL;
	return __real_malloc(n);
}

void *
__wrap_realloc(void *p, size_t n)
{
	if (p == NULL)
		return __wrap_malloc(n);
	if (n == 0) {
		free(p);
		return NULL;
	}
	return __real_realloc(p, n);
}

void *
__wrap_reallocarray(void *p, size_t count, size_t size)
{
	if (count != 0 && size > SIZE_MAX / count) {
		errno = ENOMEM;
		return NULL;
	}
	return __wrap_realloc(p, count * size);
}
