/*
 * Host gate for usr.bin/textbox/getline.c, included here so its static
 * capacity policy is reachable, over a malloc()/realloc() the test owns
 * so a refused growth is a value the test sets. Input comes from
 * fmemopen(); a stream that fails part way through a line comes from
 * fopencookie() on glibc and funopen() elsewhere.
 *
 * The decisive cases: a refused growth returns -1 and leaves the caller
 * holding the block it passed, with the bytes read so far terminated
 * and the stream positioned to continue; and the capacity policy holds
 * at SSIZE_MAX + 1 instead of wrapping, at both widths.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Every allocation getline.c makes goes through these, and the function
 * itself takes another name, since the host's stdio.h declares getline
 * with nonnull parameters and would reject the EINVAL cases at compile
 * time.
 */
static void *t_malloc(size_t);
static void *t_realloc(void *, size_t);
#define malloc t_malloc
#define realloc t_realloc
#define getline t_getline
#include "../getline.c"
#undef malloc
#undef realloc
#undef getline

/* Live blocks, so ownership is a fact the test can look up. */
#define LIVE_MAX 16
static void *live[LIVE_MAX];
static size_t live_size[LIVE_MAX];
static int realloc_refusals;	/* refuse this many, then allow */
static int malloc_calls, realloc_calls;

static int
slot_of(const void *p)
{
	int i;

	for (i = 0; i < LIVE_MAX; i++)
		if (p != NULL && live[i] == p)
			return i;
	return -1;
}

static void *
t_malloc(size_t n)
{
	void *p;
	int i;

	malloc_calls++;
	if (realloc_refusals > 0) {
		realloc_refusals--;
		errno = ENOMEM;
		return NULL;
	}
	p = malloc(n);
	for (i = 0; i < LIVE_MAX && p != NULL; i++)
		if (live[i] == NULL) {
			live[i] = p;
			live_size[i] = n;
			break;
		}
	return p;
}

static void *
t_realloc(void *old, size_t n)
{
	void *p;
	int i;

	if (old == NULL)
		return t_malloc(n);
	realloc_calls++;
	i = slot_of(old);
	if (i < 0) {
		fprintf(stderr, "realloc of a block the test never handed out\n");
		abort();
	}
	if (realloc_refusals > 0) {
		realloc_refusals--;
		errno = ENOMEM;
		return NULL;
	}
	p = realloc(old, n);
	if (p != NULL) {
		live[i] = p;
		live_size[i] = n;
	}
	return p;
}

static void
t_free(void *p)
{
	int i = slot_of(p);

	if (i >= 0) {
		live[i] = NULL;
		live_size[i] = 0;
	}
	free(p);
}

static int live_count(void)
{
	int i, n = 0;

	for (i = 0; i < LIVE_MAX; i++)
		if (live[i] != NULL)
			n++;
	return n;
}

static int failures, checks;

static void
check(int cond, const char *what)
{
	checks++;
	if (!cond) {
		failures++;
		fprintf(stderr, "getline test: FAIL: %s\n", what);
	}
}

static FILE *
input(const char *text)
{
	FILE *f = fmemopen((void *)text, strlen(text), "r");

	if (f == NULL) {
		perror("fmemopen");
		exit(2);
	}
	return f;
}

/* A stream that yields "ab" and then fails with EIO. */
static int failing_reads;

#ifdef __GLIBC__
static ssize_t
failing_read(void *cookie, char *buf, size_t size)
{
	(void)cookie;
	if (failing_reads++ == 0 && size >= 2) {
		buf[0] = 'a'; buf[1] = 'b';
		return 2;
	}
	errno = EIO;
	return -1;
}

static FILE *
failing_stream(void)
{
	cookie_io_functions_t io = { failing_read, NULL, NULL, NULL };

	failing_reads = 0;
	return fopencookie(NULL, "r", io);
}
#else
static int
failing_read(void *cookie, char *buf, int size)
{
	(void)cookie;
	if (failing_reads++ == 0 && size >= 2) {
		buf[0] = 'a'; buf[1] = 'b';
		return 2;
	}
	errno = EIO;
	return -1;
}

static FILE *
failing_stream(void)
{
	failing_reads = 0;
	return funopen(NULL, failing_read, NULL, NULL, NULL);
}
#endif

static void
argument_contract(void)
{
	char *p = NULL;
	size_t n = 0;
	FILE *f = input("x\n");

	errno = 0;
	check(t_getline(NULL, &n, f) == -1 && errno == EINVAL, "NULL lineptr is EINVAL");
	errno = 0;
	check(t_getline(&p, NULL, f) == -1 && errno == EINVAL, "NULL n is EINVAL");
	errno = 0;
	check(t_getline(&p, &n, NULL) == -1 && errno == EINVAL, "NULL stream is EINVAL");
	check(p == NULL && n == 0 && live_count() == 0, "an EINVAL call allocates nothing");
	fclose(f);
}

static void
line_contract(void)
{
	char *p = NULL;
	size_t n = 0;
	FILE *f = input("abc\nde\n\nlast");
	ssize_t r;

	r = t_getline(&p, &n, f);
	check(r == 4 && strcmp(p, "abc\n") == 0, "a line with its newline");
	check(n == GETLINE_FIRST && slot_of(p) >= 0 && live_size[slot_of(p)] == n,
	    "a fresh buffer is the first capacity");
	r = t_getline(&p, &n, f);
	check(r == 3 && strcmp(p, "de\n") == 0, "the buffer is reused");
	r = t_getline(&p, &n, f);
	check(r == 1 && strcmp(p, "\n") == 0, "an empty line is one byte");
	r = t_getline(&p, &n, f);
	check(r == 4 && strcmp(p, "last") == 0, "a final line without a newline");
	r = t_getline(&p, &n, f);
	check(r == -1 && feof(f) && !ferror(f), "end of file is -1 with feof");
	check(p[0] == '\0', "the buffer is terminated at end of file");
	check(live_count() == 1, "one buffer over the whole file");
	t_free(p);
	fclose(f);

	/* Nothing at all: -1, and a buffer the caller now owns. */
	p = NULL; n = 0;
	f = input("");
	r = t_getline(&p, &n, f);
	check(r == -1 && feof(f) && p != NULL && n == GETLINE_FIRST && p[0] == '\0',
	    "an empty stream is -1 with an allocated, terminated buffer");
	t_free(p);
	fclose(f);
}

static void
growth_contract(void)
{
	char text[400];
	char *p = NULL;
	size_t n = 0;
	FILE *f;
	ssize_t r;

	memset(text, 'x', 299);
	text[299] = '\n';
	text[300] = '\0';
	f = input(text);
	realloc_calls = 0;
	r = t_getline(&p, &n, f);
	check(r == 300 && memcmp(p, text, 300) == 0 && p[300] == '\0',
	    "a 300-byte line is read whole");
	check(n == 512 && realloc_calls == 2, "128 doubles to 256 and 512");
	check(live_count() == 1 && live_size[slot_of(p)] == 512,
	    "the caller holds the grown block");
	t_free(p);
	fclose(f);

	/* A caller's small buffer grows from its own size. */
	p = t_malloc(4); n = 4;
	f = input("abcdefgh\n");
	r = t_getline(&p, &n, f);
	check(r == 9 && strcmp(p, "abcdefgh\n") == 0 && n == 16,
	    "a 4-byte caller buffer grows to 16");
	t_free(p);
	fclose(f);

	/* A caller pointer with a zero size is resized, not replaced. */
	p = t_malloc(8); n = 0;
	f = input("ab\n");
	r = t_getline(&p, &n, f);
	check(r == 3 && n == GETLINE_FIRST && live_count() == 1,
	    "a zero-size caller buffer is resized and the old block is not leaked");
	t_free(p);
	fclose(f);
}

static void
failure_contract(void)
{
	char *p = NULL, *before;
	size_t n = 0, nbefore;
	FILE *f;
	ssize_t r;

	/* The first allocation refused: nothing is the caller's. */
	f = input("abc\n");
	realloc_refusals = 1;
	errno = 0;
	r = t_getline(&p, &n, f);
	check(r == -1 && errno == ENOMEM, "a refused first allocation is -1, ENOMEM");
	check(p == NULL && n == 0 && live_count() == 0,
	    "a refused first allocation leaves lineptr and n as they were");
	realloc_refusals = 0;
	fclose(f);

	/* Growth refused part way through a line: the caller keeps the
	 * block realloc refused to resize, terminated, and the stream
	 * continues from the byte that did not fit. */
	p = t_malloc(4); n = 4;
	before = p; nbefore = n;
	f = input("abcdef\nrest\n");
	realloc_refusals = 1;
	errno = 0;
	r = t_getline(&p, &n, f);
	check(r == -1 && errno == ENOMEM, "a refused growth is -1, ENOMEM");
	check(p == before && n == nbefore, "a refused growth hands back the same block and size");
	check(slot_of(p) >= 0 && live_size[slot_of(p)] == 4, "that block is still allocated");
	check(strcmp(p, "abc") == 0, "the bytes read so far are terminated");
	check(!feof(f) && !ferror(f), "the stream carries no error of its own");
	realloc_refusals = 0;
	r = t_getline(&p, &n, f);
	check(r == 4 && strcmp(p, "def\n") == 0 && n == 8,
	    "the next call continues from the byte that did not fit");
	r = t_getline(&p, &n, f);
	check(r == 5 && strcmp(p, "rest\n") == 0, "and the line after it");
	check(live_count() == 1, "one block throughout");
	t_free(p);
	fclose(f);

	/* The stream fails after two bytes: -1 with ferror, the bytes kept. */
	p = NULL; n = 0;
	f = failing_stream();
	check(f != NULL, "a failing stream can be built");
	if (f != NULL) {
		r = t_getline(&p, &n, f);
		check(r == -1 && ferror(f), "a stream error is -1 with ferror");
		check(p != NULL && strcmp(p, "ab") == 0, "the bytes before the error are terminated");
		check(live_count() == 1, "the buffer is the caller's after a stream error");
		t_free(p);
		fclose(f);
	}
}

/* The capacity policy at the width the binary was built for. */
static void
capacity_contract(void)
{
	const size_t max = GETLINE_CAP_MAX;

	check(getline_grow(128) == 256, "128 grows to 256");
	check(getline_grow(max / 2) == max, "half the maximum grows to the maximum");
	check(getline_grow(max / 2 + 1) == max, "past half grows to the maximum, not past it");
	check(getline_grow(max - 1) == max, "one below the maximum grows to it");
	check(getline_grow(max) == 0, "the maximum grows no further");
	check(getline_grow(SIZE_MAX) == 0, "SIZE_MAX grows no further");
	check(max - 1 == (size_t)SSIZE_MAX, "the maximum is one past SSIZE_MAX");
#ifdef HK_ILP32
	check(sizeof(size_t) == 4 && sizeof(ssize_t) == 4, "the target's four-byte sizes");
	check(max == 0x80000000u, "the maximum is 2^31 at the target's width");
#endif
}

int
main(void)
{
	argument_contract();
	line_contract();
	growth_contract();
	failure_contract();
	capacity_contract();
	if (failures) {
		fprintf(stderr, "getline test: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("getline test: %d checks passed\n", checks);
	return 0;
}
