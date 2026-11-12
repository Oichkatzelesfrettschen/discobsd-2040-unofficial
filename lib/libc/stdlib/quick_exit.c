#include <stdlib.h>

#include "quick_exit.h"

struct quick_exit_entry *__quick_exit_head;

_Noreturn void
quick_exit(int status)
{
	struct quick_exit_entry *entry;

	/* Removal precedes callbacks so successful registrations join the live list. */
	while ((entry = __quick_exit_head) != NULL) {
		__quick_exit_head = entry->next;
		entry->function();
	}
	_Exit(status);
}
