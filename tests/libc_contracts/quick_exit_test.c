#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int db_at_quick_exit(void (*)(void));
int db_atexit(void (*)(void));
_Noreturn void db_quick_exit(int);
_Noreturn void db_exit(int);

static char events[128];
static size_t event_count;
static const char *expected;
static int expected_status = 7;
static void *allocations[128];
static size_t allocation_count;
static int refuse_allocation;
static unsigned repeat_count;

static void
record(char event)
{
	if (event_count + 1 >= sizeof(events)) {
		fputs("quick-exit mismatch: callback loop\n", stderr);
		_Exit(1);
	}
	events[event_count++] = event;
}

void *
quick_test_malloc(size_t size)
{
	void *allocation;

	if (refuse_allocation || allocation_count == 128)
		return NULL;
	allocation = malloc(size);
	if (allocation != NULL)
		allocations[allocation_count++] = allocation;
	return allocation;
}

static void
register_handler(void (*handler)(void))
{
	if (db_at_quick_exit(handler) != 0) {
		fputs("quick-exit registration mismatch: available storage fails\n", stderr);
		_Exit(1);
	}
}

static void handler_a(void) { record('A'); }
static void handler_c(void) { record('C'); }
static void handler_d(void) { record('D'); }
static void handler_n(void) { record('N'); }

static void
handler_b(void)
{
	record('B');
	register_handler(handler_c);
	register_handler(handler_d);
}

static void
handler_repeat(void)
{
	record('R');
	if (++repeat_count < 3)
		register_handler(handler_repeat);
}

void
quick_test_cleanup(void)
{
	record('!');
}

_Noreturn void
quick_test_terminate(int status)
{
	int result = status != expected_status || strcmp(events, expected) != 0;

	if (result)
		fprintf(stderr, "quick-exit mismatch: events=%s expected=%s status=%d\n",
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
		expected = "BDCA";
		register_handler(handler_a);
		register_handler(handler_b);
	} else if (strcmp(argv[1], "last") == 0) {
		expected = "BDC";
		register_handler(handler_b);
	} else if (strcmp(argv[1], "duplicate") == 0) {
		expected = "AA";
		register_handler(handler_a);
		register_handler(handler_a);
	} else if (strcmp(argv[1], "capacity") == 0) {
		expected = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
		for (int index = 0; index < 32; index++)
			register_handler(handler_a);
	} else if (strcmp(argv[1], "failure") == 0 ||
	    strcmp(argv[1], "retry") == 0) {
		expected = "A";
		register_handler(handler_a);
		refuse_allocation = 1;
		if (db_at_quick_exit(handler_b) == 0) {
			fputs("quick-exit registration mismatch: refused storage succeeds\n", stderr);
			return 1;
		}
		if (strcmp(argv[1], "retry") == 0) {
			refuse_allocation = 0;
			register_handler(handler_c);
			expected = "CA";
		}
	} else if (strcmp(argv[1], "first-no-heap") == 0) {
		expected = "A";
		refuse_allocation = 1;
		register_handler(handler_a);
	} else if (strcmp(argv[1], "last-repeat") == 0) {
		expected = "RRR";
		refuse_allocation = 1;
		register_handler(handler_repeat);
	} else if (strcmp(argv[1], "normal-isolation") == 0 ||
	    strcmp(argv[1], "quick-isolation") == 0) {
		if (db_atexit(handler_n) != 0)
			return 2;
		register_handler(handler_a);
		expected = "A";
		if (strcmp(argv[1], "normal-isolation") == 0) {
			expected = "N!";
			db_exit(expected_status);
		}
	} else if (strcmp(argv[1], "negative-status") == 0) {
		expected = "";
		expected_status = -7;
	} else if (strcmp(argv[1], "empty") == 0) {
		expected = "";
	} else {
		return 2;
	}
	db_quick_exit(expected_status);
}
