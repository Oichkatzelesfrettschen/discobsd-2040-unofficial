#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int array_memory_link_contract_enabled;

int
main(void)
{
	char *line = NULL;
	size_t capacity = 0;
	static const char bytes[] = "abc";

	if (array_memory_link_contract_enabled) {
		line = reallocarray(line, 2, sizeof(*line));
		(void)memmem(bytes, sizeof(bytes), "b", 1);
		(void)getdelim(&line, &capacity, ':', stdin);
		(void)getline(&line, &capacity, stdin);
	}
	free(line);
	return 0;
}
