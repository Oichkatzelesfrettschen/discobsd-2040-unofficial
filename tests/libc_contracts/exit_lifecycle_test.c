#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int db_atexit(void (*)(void));
_Noreturn void db_exit(int);

static char events[64];
static size_t event_count;
static const char *expected;
static void *allocations[64];
static size_t allocation_count;
static int refuse_allocation;

static void
record(char event)
{
	if (event_count + 1 >= sizeof(events)) {
		fputs("lifecycle mismatch: callback loop\n", stderr);
		_Exit(1);
	}
	events[event_count++] = event;
}

void *
exit_test_malloc(size_t size)
{
	void *allocation;

	if (refuse_allocation || allocation_count == 64)
		return NULL;
	allocation = malloc(size);
	if (allocation != NULL)
		allocations[allocation_count++] = allocation;
	return allocation;
}

static void
register_handler(void (*handler)(void))
{
	if (db_atexit(handler) != 0) {
		fputs("registration mismatch: available allocation fails\n", stderr);
		_Exit(1);
	}
}

static void handler_a(void) { record('A'); }
static void handler_c(void) { record('C'); }
static void handler_d(void) { record('D'); }

static void
handler_b(void)
{
	record('B');
	register_handler(handler_c);
	register_handler(handler_d);
}

void
exit_test_cleanup(void)
{
	record('!');
}

_Noreturn void
exit_test_terminate(int status)
{
	int result = status != 7 || strcmp(events, expected) != 0;

	if (result)
		fprintf(stderr, "lifecycle mismatch: events=%s expected=%s status=%d\n",
		    events, expected, status);
	for (size_t index = 0; index < allocation_count; index++)
		free(allocations[index]);
	_Exit(result);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "nested") == 0) {
		expected = "BDCA!";
		register_handler(handler_a);
		register_handler(handler_b);
	} else if (strcmp(argv[1], "last") == 0) {
		expected = "BDC!";
		register_handler(handler_b);
	} else if (strcmp(argv[1], "duplicate") == 0) {
		expected = "AA!";
		register_handler(handler_a);
		register_handler(handler_a);
	} else if (strcmp(argv[1], "capacity") == 0) {
		expected = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA!";
		for (int index = 0; index < 32; index++)
			register_handler(handler_a);
	} else if (strcmp(argv[1], "failure") == 0) {
		expected = "A!";
		register_handler(handler_a);
		refuse_allocation = 1;
		if (db_atexit(handler_b) == 0) {
			fputs("registration mismatch: refused allocation succeeds\n", stderr);
			return 1;
		}
	} else if (strcmp(argv[1], "empty") == 0) {
		expected = "!";
	} else {
		return 2;
	}
	db_exit(7);
}
