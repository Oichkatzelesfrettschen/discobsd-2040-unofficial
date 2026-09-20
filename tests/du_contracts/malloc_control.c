#include <stddef.h>
#include <stdlib.h>

void *
du_test_malloc(size_t size)
{
	if (getenv("DU_FAIL_ALLOC") != NULL)
		return NULL;
	return malloc(size);
}

void
du_test_free(void *allocation)
{
	free(allocation);
}
