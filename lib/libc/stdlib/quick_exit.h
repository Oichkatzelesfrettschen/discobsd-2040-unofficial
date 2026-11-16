#ifndef LIBC_STDLIB_QUICK_EXIT_H
#define LIBC_STDLIB_QUICK_EXIT_H

struct quick_exit_entry {
	struct quick_exit_entry *next;
	void (*function)(void);
};

/* Separate archive members keep allocation out of quick_exit-only callers. */
extern struct quick_exit_entry *__quick_exit_head;

#endif
