#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int db_at_quick_exit(void (*)(void));
_Noreturn void db_quick_exit(int);

static void
normal_handler(void)
{
	_exit(99);
}

static void
quick_handler(void)
{
	if (write(STDOUT_FILENO, "Q", 1) != 1)
		_exit(98);
}

int
main(void)
{
	static char buffer[BUFSIZ];

	if (atexit(normal_handler) != 0 ||
	    db_at_quick_exit(quick_handler) != 0 ||
	    setvbuf(stdout, buffer, _IOFBF, sizeof(buffer)) != 0)
		return 97;
	fputs("unflushed", stdout);
	db_quick_exit(7);
}
