#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int array_memory_link_contract_enabled;

int
main(void)
{
#if defined(ARRAY_MEMORY_LINK_REALLOCARRAY)
	char *line = NULL;

	if (array_memory_link_contract_enabled)
		line = reallocarray(line, 2, sizeof(*line));
	free(line);
#elif defined(ARRAY_MEMORY_LINK_MEMMEM)
	static const char bytes[] = "abc";

	if (array_memory_link_contract_enabled)
		(void)memmem(bytes, sizeof(bytes), "b", 1);
#elif defined(ARRAY_MEMORY_LINK_GETDELIM)
	char *line = NULL;
	size_t capacity = 0;

	if (array_memory_link_contract_enabled)
		(void)getdelim(&line, &capacity, ':', stdin);
	free(line);
#elif defined(ARRAY_MEMORY_LINK_GETLINE)
	char *line = NULL;
	size_t capacity = 0;

	if (array_memory_link_contract_enabled)
		(void)getline(&line, &capacity, stdin);
	free(line);
#elif !defined(ARRAY_MEMORY_LINK_EMPTY)
#error "select one array-memory link contract"
#endif
	return 0;
}
