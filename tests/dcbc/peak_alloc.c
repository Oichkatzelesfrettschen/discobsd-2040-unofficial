/*
 * Allocation accounting for a host build of dc, linked with
 * -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=reallocarray,
 * --wrap=strdup,--wrap=free. It keeps the requested size of every live block
 * in a hidden header, tracks the largest live total, and writes
 * "peak=<bytes> blocks=<count>" to the file named by DCBC_PEAK_FILE when the
 * process exits. The figure counts requested bytes: allocator headers and
 * rounding on the target add to it. A zero-byte request returns NULL, and
 * realloc(p, 0) frees p, as the tree's malloc does (see zero_alloc.c).
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

struct hdr {
	size_t size;
	size_t pad;
};

static size_t live, peak, blocks, installed;

static void
report(void)
{
	const char *path = getenv("DCBC_PEAK_FILE");
	FILE *f;

	if (path == NULL)
		return;
	f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f, "peak=%zu blocks=%zu\n", peak, blocks);
	fclose(f);
}

static void
account(size_t added)
{
	if (!installed) {
		installed = 1;
		atexit(report);
	}
	live += added;
	if (live > peak)
		peak = live;
}

void *
__wrap_malloc(size_t n)
{
	struct hdr *h;

	if (n == 0)
		return NULL;
	h = __real_malloc(sizeof(*h) + n);
	if (h == NULL)
		return NULL;
	h->size = n;
	blocks++;
	account(n);
	return h + 1;
}

void *
__wrap_calloc(size_t a, size_t b)
{
	size_t n;
	void *p;

	if (b != 0 && a > (size_t)-1 / b)
		return NULL;
	n = a * b;
	p = __wrap_malloc(n);
	if (p != NULL)
		memset(p, 0, n);
	return p;
}

void
__wrap_free(void *p)
{
	struct hdr *h;

	if (p == NULL)
		return;
	h = (struct hdr *)p - 1;
	live -= h->size;
	blocks--;
	__real_free(h);
}

void *
__wrap_realloc(void *p, size_t n)
{
	struct hdr *h;
	void *q;

	if (p == NULL)
		return __wrap_malloc(n);
	if (n == 0) {
		__wrap_free(p);
		return NULL;
	}
	h = (struct hdr *)p - 1;
	q = __wrap_malloc(n);
	if (q == NULL)
		return NULL;
	memcpy(q, p, h->size < n ? h->size : n);
	__wrap_free(p);
	return q;
}

void *
__wrap_reallocarray(void *p, size_t a, size_t b)
{
	if (b != 0 && a > (size_t)-1 / b)
		return NULL;
	return __wrap_realloc(p, a * b);
}

char *
__wrap_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __wrap_malloc(n);

	if (p != NULL)
		memcpy(p, s, n);
	return p;
}
