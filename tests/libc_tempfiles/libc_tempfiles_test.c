#include <sys/param.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *discobsd_tmpfile(void);
char *discobsd_tmpnam(char *);
char *discobsd_tempnam(const char *, const char *);

static char operation_log[16];
static size_t operation_count;
static char created_path[MAXPATHLEN];
static char candidate_paths[4][MAXPATHLEN];
static int candidate_count;
static int mkstemp_result;
static int unlink_result;
static FILE *fdopen_result;
static const char *environment_tmpdir;
static int mktemp_success_call;
static unsigned long free_count;
static size_t allocation_sizes[8];
static size_t allocation_count;

static void
record_operation(char operation)
{
	operation_log[operation_count++] = operation;
	operation_log[operation_count] = '\0';
}

int
test_mkstemp(char *pathname)
{
	record_operation('m');
	strcpy(created_path, pathname);
	if (mkstemp_result < 0)
		errno = ENOSPC;
	return mkstemp_result;
}

int
test_unlink(const char *pathname)
{
	record_operation('u');
	if (strcmp(pathname, created_path) != 0)
		return -1;
	if (unlink_result < 0)
		errno = EACCES;
	return unlink_result;
}

FILE *
test_fdopen(int descriptor, const char *mode)
{
	record_operation('f');
	if (descriptor != mkstemp_result || strcmp(mode, "w+") != 0)
		return NULL;
	if (fdopen_result == NULL)
		errno = EMFILE;
	return fdopen_result;
}

int
test_close(int descriptor)
{
	record_operation('c');
	if (descriptor != mkstemp_result)
		return -1;
	errno = EBADF;
	return 0;
}

char *
test_getenv(const char *name)
{
	if (strcmp(name, "TMPDIR") != 0)
		return NULL;
	return (char *)environment_tmpdir;
}

char *
test_mktemp(char *pathname)
{
	if (candidate_count < 4)
		strcpy(candidate_paths[candidate_count], pathname);
	if (candidate_count++ == mktemp_success_call)
		return pathname;
	errno = EACCES;
	return NULL;
}

void *
test_malloc(size_t size)
{
	if (allocation_count < sizeof(allocation_sizes) / sizeof(allocation_sizes[0]))
		allocation_sizes[allocation_count] = size;
	++allocation_count;
	return malloc(size);
}

void
test_free(void *pointer)
{
	++free_count;
	free(pointer);
}

static void
fail(const char *message)
{
	fprintf(stderr, "libc tempfiles test: %s\n", message);
	exit(1);
}

static void
require(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

static void
reset_mocks(void)
{
	operation_count = 0;
	operation_log[0] = '\0';
	created_path[0] = '\0';
	candidate_count = 0;
	mkstemp_result = 23;
	unlink_result = 0;
	fdopen_result = (FILE *)(uintptr_t)1;
	environment_tmpdir = NULL;
	mktemp_success_call = 0;
	allocation_count = 0;
}

int
main(void)
{
	char caller_path[MAXPATHLEN];
	char oversized_directory[MAXPATHLEN + 1];
	char *allocated_path;
	char *first_internal_path;
	char *second_internal_path;
	FILE *stream;
	unsigned long frees_before;

	reset_mocks();
	stream = discobsd_tmpfile();
	require(stream == fdopen_result, "tmpfile success result");
	require(strcmp(operation_log, "muf") == 0,
	    "tmpfile success operation order");
	require(strcmp(created_path, "/tmp/XXXXXX") == 0,
	    "tmpfile template");

	reset_mocks();
	mkstemp_result = -1;
	errno = 0;
	require(discobsd_tmpfile() == NULL && errno == ENOSPC,
	    "tmpfile mkstemp failure");
	require(strcmp(operation_log, "m") == 0,
	    "tmpfile continues after mkstemp failure");

	reset_mocks();
	unlink_result = -1;
	errno = 0;
	require(discobsd_tmpfile() == NULL && errno == EACCES,
	    "tmpfile unlink failure");
	require(strcmp(operation_log, "muc") == 0,
	    "tmpfile unlink-failure cleanup");

	reset_mocks();
	fdopen_result = NULL;
	errno = 0;
	require(discobsd_tmpfile() == NULL && errno == EMFILE,
	    "tmpfile fdopen failure");
	require(strcmp(operation_log, "mufc") == 0,
	    "tmpfile fdopen-failure cleanup");

	reset_mocks();
	require(discobsd_tmpnam(caller_path) == caller_path,
	    "tmpnam caller storage");
	require(strcmp(caller_path, "/tmp/XXXXXX") == 0,
	    "tmpnam template");
	reset_mocks();
	first_internal_path = discobsd_tmpnam(NULL);
	require(first_internal_path != NULL, "tmpnam internal storage result");
	reset_mocks();
	second_internal_path = discobsd_tmpnam(NULL);
	require(second_internal_path == first_internal_path,
	    "tmpnam internal storage reuse");
	require(allocation_count == 0, "tmpnam internal storage allocation");
	reset_mocks();
	mktemp_success_call = -1;
	frees_before = free_count;
	require(discobsd_tmpnam(NULL) == NULL,
	    "tmpnam failure result");
	require(free_count == frees_before && allocation_count == 0,
	    "tmpnam failure uses internal storage");

	reset_mocks();
	environment_tmpdir = "/environment";
	allocated_path = discobsd_tempnam("/argument", "pre");
	require(allocated_path != NULL, "tempnam TMPDIR result");
	require(strcmp(candidate_paths[0], "/environment/preXXXXXX") == 0,
	    "tempnam TMPDIR candidate");
	require(allocation_count == 1 &&
	    allocation_sizes[0] == sizeof("/environment/preXXXXXX"),
	    "tempnam TMPDIR exact allocation");
	free(allocated_path);

	reset_mocks();
	environment_tmpdir = "/environment/";
	allocated_path = discobsd_tempnam(NULL, NULL);
	require(allocated_path != NULL, "tempnam null prefix result");
	require(strcmp(candidate_paths[0], "/environment/XXXXXX") == 0,
	    "tempnam null prefix candidate");
	require(allocation_count == 1 &&
	    allocation_sizes[0] == sizeof("/environment/XXXXXX"),
	    "tempnam null prefix exact allocation");
	free(allocated_path);

	reset_mocks();
	environment_tmpdir = "/environment";
	mktemp_success_call = 1;
	frees_before = free_count;
	allocated_path = discobsd_tempnam("/argument", "p");
	require(allocated_path != NULL, "tempnam directory fallback result");
	require(strcmp(candidate_paths[0], "/environment/pXXXXXX") == 0 &&
	    strcmp(candidate_paths[1], "/argument/pXXXXXX") == 0,
	    "tempnam directory fallback order");
	require(allocation_count == 2 &&
	    allocation_sizes[0] == sizeof("/environment/pXXXXXX") &&
	    allocation_sizes[1] == sizeof("/argument/pXXXXXX"),
	    "tempnam fallback exact allocations");
	require(free_count == frees_before + 1,
	    "tempnam rejected fallback allocation cleanup");
	free(allocated_path);

	memset(oversized_directory, 'x', sizeof(oversized_directory) - 1);
	oversized_directory[sizeof(oversized_directory) - 1] = '\0';
	reset_mocks();
	environment_tmpdir = oversized_directory;
	allocated_path = discobsd_tempnam("/argument", "p");
	require(allocated_path != NULL, "tempnam oversized TMPDIR fallback");
	require(candidate_count == 1 &&
	    strcmp(candidate_paths[0], "/argument/pXXXXXX") == 0,
	    "tempnam uses an oversized TMPDIR");
	require(allocation_count == 1 &&
	    allocation_sizes[0] == sizeof("/argument/pXXXXXX"),
	    "tempnam skips oversized allocation");
	free(allocated_path);

	reset_mocks();
	mktemp_success_call = -1;
	frees_before = free_count;
	require(discobsd_tempnam(NULL, NULL) == NULL,
	    "tempnam exhaustion result");
	require(allocation_count == 2 && free_count == frees_before + 2,
	    "tempnam exhaustion allocation cleanup");

	puts("libc tempfiles tests passed");
	return 0;
}
