/* The maintained target shell uses the production GNU17 language contract. */
#include "../defs.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L
#error "bin/sh requires GNU17 or strict C17"
#endif

static void
check_execution_input_types(void)
{
	void (*argument_input)(char **, int) = execute_argument_input;
	void (*descriptor_input)(int, int) = execute_descriptor_input;
	void (*string_input)(char *, int) = execute_string_input;

	(void)argument_input;
	(void)descriptor_input;
	(void)string_input;
}

int
main(void)
{
	check_execution_input_types();
	return 0;
}
