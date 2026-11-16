#include <stdlib.h>

#include "quick_exit.h"

int
at_quick_exit(void (*function)(void))
{
	static struct quick_exit_entry first;
	struct quick_exit_entry *entry;

	if (__quick_exit_head == NULL) {
		entry = &first;
	} else {
		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			return -1;
	}
	entry->function = function;
	entry->next = __quick_exit_head;
	__quick_exit_head = entry;
	return 0;
}
