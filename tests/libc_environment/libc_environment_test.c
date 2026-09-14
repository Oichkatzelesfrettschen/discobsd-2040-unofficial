#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char **test_environ;

int discobsd_setenv(const char *, const char *, int);
int discobsd_unsetenv(const char *);
int discobsd_putenv(char *);
char *discobsd_getenv(const char *);
void *discobsd_test_malloc(size_t);
void discobsd_test_free(void *);

static long allocation_failure_countdown = -1;
static unsigned long free_count;

void *
discobsd_test_malloc(size_t size)
{
	if (allocation_failure_countdown == 0) {
		errno = ENOMEM;
		return NULL;
	}
	if (allocation_failure_countdown > 0)
		--allocation_failure_countdown;
	return malloc(size);
}

void
discobsd_test_free(void *pointer)
{
	++free_count;
	free(pointer);
}

static void
fail(const char *message)
{
	fprintf(stderr, "libc environment test: %s\n", message);
	exit(1);
}

static void
require(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

static void
require_value(const char *name, const char *expected)
{
	char *actual;

	actual = discobsd_getenv(name);
	if (actual == NULL || strcmp(actual, expected) != 0)
		fail(name);
}

int
main(void)
{
	char initial_first[] = "DUP=first";
	char initial_second[] = "DUP=second";
	char mode_entry[] = "MODE=borrowed";
	char append_entry[] = "APPEND=borrowed";
	char duplicate_first[] = "REMOVE=one";
	char duplicate_second[] = "REMOVE=two";
	char failure_entry[] = "FAIL=old";
	char putenv_failure_entry[] = "PUTFAIL=value";
	char *alias_environment[] = { NULL };
	char *initial_environment[] = { initial_first, initial_second, NULL };
	char *mode_environment[] = { mode_entry, NULL };
	char *duplicate_environment[] = {
		duplicate_first, duplicate_second, NULL
	};
	char *failure_environment[] = { failure_entry, NULL };
	char *putenv_failure_environment[] = { putenv_failure_entry, NULL };
	char **preserved_environment;
	char *preserved_entry;
	char *alias_entry;
	unsigned long frees_before;

	test_environ = initial_environment;
	require_value("DUP", "first");
	require(discobsd_getenv("MISSING") == NULL, "missing lookup");

	errno = 0;
	require(discobsd_setenv("", "value", 1) == -1 && errno == EINVAL,
	    "setenv accepts an empty name");
	errno = 0;
	require(discobsd_setenv("BAD=NAME", "value", 1) == -1 &&
	    errno == EINVAL, "setenv accepts '=' in a name");
	errno = 0;
	require(discobsd_setenv("BAD=", "value", 1) == -1 &&
	    errno == EINVAL, "setenv accepts a trailing '=' in a name");
	errno = 0;
	require(discobsd_unsetenv("") == -1 && errno == EINVAL,
	    "unsetenv accepts an empty name");
	errno = 0;
	require(discobsd_unsetenv(NULL) == -1 && errno == EINVAL,
	    "unsetenv accepts a null name");
	errno = 0;
	require(discobsd_unsetenv("BAD=") == -1 && errno == EINVAL,
	    "unsetenv accepts a trailing '=' in a name");
	errno = 0;
	require(discobsd_putenv((char *)"MALFORMED") == -1 && errno == EINVAL,
	    "putenv accepts a string without '='");

	preserved_environment = test_environ;
	require(discobsd_setenv("DUP", "ignored", 0) == 0,
	    "setenv rewrite=0 fails");
	require(test_environ == preserved_environment,
	    "setenv rewrite=0 replaces the environment vector");
	require_value("DUP", "first");

	test_environ = mode_environment;
	require(discobsd_setenv("MODE", "owned", 1) == 0,
	    "borrowed-to-owned replacement fails");
	require_value("MODE", "owned");
	require(strcmp(mode_entry, "MODE=borrowed") == 0,
	    "setenv modifies a borrowed string");
	frees_before = free_count;
	require(discobsd_putenv(mode_entry) == 0,
	    "owned-to-borrowed replacement fails");
	require(free_count == frees_before + 1,
	    "putenv does not release the replaced owned string");
	require_value("MODE", "borrowed");
	strcpy(mode_entry, "MODE=changed");
	require_value("MODE", "changed");

	require(discobsd_putenv(append_entry) == 0, "putenv append fails");
	require_value("APPEND", "borrowed");
	strcpy(append_entry, "APPEND=changed");
	require_value("APPEND", "changed");

	test_environ = duplicate_environment;
	require(discobsd_unsetenv("REMOVE") == 0,
	    "unsetenv duplicate removal fails");
	require(discobsd_getenv("REMOVE") == NULL,
	    "unsetenv leaves a duplicate entry");
	require(test_environ[0] == NULL, "unsetenv leaves a populated vector");

	test_environ = putenv_failure_environment;
	preserved_environment = test_environ;
	allocation_failure_countdown = 0;
	errno = 0;
	require(discobsd_putenv(append_entry) == -1 && errno == ENOMEM,
	    "putenv vector-allocation failure contract");
	require(test_environ == preserved_environment &&
	    test_environ[0] == putenv_failure_entry && test_environ[1] == NULL,
	    "putenv allocation failure changes the environment");
	allocation_failure_countdown = 0;
	errno = 0;
	require(discobsd_unsetenv("PUTFAIL") == -1 && errno == ENOMEM,
	    "unsetenv vector-allocation failure contract");
	require(test_environ == preserved_environment &&
	    test_environ[0] == putenv_failure_entry && test_environ[1] == NULL,
	    "unsetenv allocation failure changes the environment");

	test_environ = failure_environment;
	preserved_environment = test_environ;
	preserved_entry = test_environ[0];
	allocation_failure_countdown = 0;
	errno = 0;
	require(discobsd_setenv("FAIL", "new", 1) == -1 && errno == ENOMEM,
	    "setenv string-allocation failure contract");
	require(test_environ == preserved_environment &&
	    test_environ[0] == preserved_entry,
	    "string-allocation failure changes the environment");

	allocation_failure_countdown = 1;
	errno = 0;
	require(discobsd_setenv("FAIL", "new", 1) == -1 && errno == ENOMEM,
	    "setenv vector-allocation failure contract");
	require(test_environ == preserved_environment &&
	    test_environ[0] == preserved_entry,
	    "vector-allocation failure changes the environment");
	require_value("FAIL", "old");

	allocation_failure_countdown = -1;
	require(discobsd_setenv("FAIL", "new", 1) == 0,
	    "setenv does not recover after allocation failure");
	require_value("FAIL", "new");
	preserved_environment = test_environ;
	preserved_entry = test_environ[0];
	allocation_failure_countdown = 1;
	errno = 0;
	require(discobsd_setenv("GROW", "value", 1) == -1 && errno == ENOMEM,
	    "managed growth failure contract");
	require(test_environ == preserved_environment &&
	    test_environ[0] == preserved_entry && test_environ[1] == NULL,
	    "managed growth failure changes the environment");
	require_value("FAIL", "new");

	allocation_failure_countdown = -1;
	require(discobsd_setenv("GROW", "value", 1) == 0,
	    "managed growth does not recover");
	require_value("GROW", "value");
	require(discobsd_setenv("LEADING", "=value", 1) == 0,
	    "setenv leading-equals value fails");
	require_value("LEADING", "=value");

	test_environ = alias_environment;
	require(discobsd_setenv("ALIAS", "owned", 1) == 0,
	    "alias setup fails");
	alias_entry = test_environ[0];
	frees_before = free_count;
	require(discobsd_putenv(alias_entry) == 0,
	    "putenv rejects the active owned entry");
	require(test_environ[0] == alias_entry,
	    "putenv replaces its aliased entry pointer");
	require(free_count == frees_before,
	    "putenv frees the aliased entry it republishes");
	require_value("ALIAS", "owned");
	require(discobsd_unsetenv("ALIAS") == 0,
	    "unsetenv rejects the borrowed alias");
	require(free_count == frees_before,
	    "unsetenv frees an entry transferred to borrowed ownership");
	free(alias_entry);
	puts("libc environment tests passed");
	return 0;
}
