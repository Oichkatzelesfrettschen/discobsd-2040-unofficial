/*
 * lib/libc/gen/qsort.c compiled from the tree with its name moved aside.
 *
 * The decisive case is a comparator that sorts: while the outer sort of
 * 48-byte records is partitioning, its comparator calls qsort() on an
 * array of 4-byte ints. An implementation that keeps its record size and
 * comparator at file scope has them overwritten by the inner call and
 * finishes the outer partition with the inner size, which scrambles the
 * records or reads past them. A correct implementation leaves the outer
 * sort ordered, with every record intact, and the inner sorts ordered
 * too. The remaining cases pin ordering against a reference over the
 * sizes the tree uses: 1, 2, 4, 48 and an odd 7, at n of 0, 1, THRESH
 * boundaries and a few hundred.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void db_qsort(void *, size_t, size_t, int (*)(const void *, const void *));

static int failures, checks;

static void
check(int condition, const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "qsort test: FAIL: %s\n", what);
	}
}

static unsigned lcg_state = 12345;
static unsigned
lcg(void)
{
	lcg_state = lcg_state * 1103515245u + 12345u;
	return lcg_state >> 8;
}

/* A record of the size the file's thresholds were tuned for. */
struct rec48 {
	int key;
	unsigned serial;
	char pad[40];
};

static int
cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

static int
cmp_rec(const void *a, const void *b)
{
	const struct rec48 *x = a, *y = b;
	return x->key < y->key ? -1 : x->key > y->key;
}

static int
cmp_uchar(const void *a, const void *b)
{
	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

static int
cmp_short(const void *a, const void *b)
{
	short x, y;
	memcpy(&x, a, sizeof x);
	memcpy(&y, b, sizeof y);
	return x < y ? -1 : x > y;
}

/* Seven-byte records: a key in the first four bytes, unaligned. */
static int
cmp_seven(const void *a, const void *b)
{
	int x, y;
	memcpy(&x, a, sizeof x);
	memcpy(&y, b, sizeof y);
	return x < y ? -1 : x > y;
}

static int
sorted(const void *base, size_t n, size_t size,
    int (*cmp)(const void *, const void *))
{
	const char *p = base;
	size_t i;

	for (i = 1; i < n; i++)
		if (cmp(p + (i - 1) * size, p + i * size) > 0)
			return 0;
	return 1;
}

/*
 * The outer sort's comparator: it sorts a fresh array of ints of its
 * own before answering, so the inner call runs while the outer
 * partition is live, with a different record size and comparator.
 */
static unsigned inner_calls, inner_unsorted;

static int
cmp_rec_nesting(const void *a, const void *b)
{
	int inner[37];
	size_t i;

	for (i = 0; i < 37; i++)
		inner[i] = (int)(lcg() % 1000);
	db_qsort(inner, 37, sizeof inner[0], cmp_int);
	inner_calls++;
	if (!sorted(inner, 37, sizeof inner[0], cmp_int))
		inner_unsorted++;
	return cmp_rec(a, b);
}

static void
nested_sort(void)
{
	enum { N = 200 };
	static struct rec48 recs[N];
	static unsigned seen[N];
	size_t i;
	int intact = 1;

	for (i = 0; i < N; i++) {
		recs[i].key = (int)(lcg() % 50);
		recs[i].serial = (unsigned)i;
		memset(recs[i].pad, (int)i, sizeof recs[i].pad);
		seen[i] = 0;
	}
	inner_calls = inner_unsorted = 0;
	db_qsort(recs, N, sizeof recs[0], cmp_rec_nesting);
	check(inner_calls > 0, "the comparator's own sorts ran");
	check(inner_unsorted == 0, "every inner sort of ints came out ordered");
	check(sorted(recs, N, sizeof recs[0], cmp_rec),
	    "the outer sort of 48-byte records is ordered around the inner sorts");
	for (i = 0; i < N; i++) {
		unsigned s = recs[i].serial;
		if (s >= N || seen[s]++ ||
		    recs[i].pad[0] != (char)s || recs[i].pad[39] != (char)s)
			intact = 0;
	}
	check(intact, "every record survives whole and exactly once");
}

static void
sizes_and_counts(void)
{
	static const size_t counts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 17, 64, 300 };
	static unsigned char buf[300 * 48], ref[300 * 48];
	size_t c, i, n;
	char what[96];

	for (c = 0; c < sizeof counts / sizeof counts[0]; c++) {
		n = counts[c];

		for (i = 0; i < n; i++)
			buf[i] = (unsigned char)lcg();
		db_qsort(buf, n, 1, cmp_uchar);
		snprintf(what, sizeof what, "%u one-byte records", (unsigned)n);
		check(sorted(buf, n, 1, cmp_uchar), what);

		for (i = 0; i < n * 2; i++)
			buf[i] = (unsigned char)lcg();
		db_qsort(buf, n, 2, cmp_short);
		snprintf(what, sizeof what, "%u two-byte records", (unsigned)n);
		check(sorted(buf, n, 2, cmp_short), what);

		for (i = 0; i < n; i++) {
			int v = (int)(lcg() % 97) - 48;
			memcpy(buf + i * 4, &v, 4);
		}
		db_qsort(buf, n, 4, cmp_int);
		snprintf(what, sizeof what, "%u four-byte records", (unsigned)n);
		check(sorted(buf, n, 4, cmp_int), what);

		for (i = 0; i < n * 7; i++)
			buf[i] = (unsigned char)lcg();
		db_qsort(buf, n, 7, cmp_seven);
		snprintf(what, sizeof what, "%u seven-byte records", (unsigned)n);
		check(sorted(buf, n, 7, cmp_seven), what);

		for (i = 0; i < n * 48; i++)
			buf[i] = (unsigned char)lcg();
		memcpy(ref, buf, n * 48);
		db_qsort(buf, n, 48, cmp_rec);
		snprintf(what, sizeof what, "%u 48-byte records", (unsigned)n);
		check(sorted(buf, n, 48, cmp_rec), what);
	}
}

/* Equal keys, all keys equal, already sorted, and reversed. */
static void
shapes(void)
{
	static int a[257];
	size_t i;

	for (i = 0; i < 257; i++)
		a[i] = 7;
	db_qsort(a, 257, sizeof a[0], cmp_int);
	check(sorted(a, 257, sizeof a[0], cmp_int) && a[0] == 7 && a[256] == 7,
	    "all keys equal");
	for (i = 0; i < 257; i++)
		a[i] = (int)i;
	db_qsort(a, 257, sizeof a[0], cmp_int);
	check(sorted(a, 257, sizeof a[0], cmp_int) && a[0] == 0 && a[256] == 256,
	    "already sorted");
	for (i = 0; i < 257; i++)
		a[i] = 256 - (int)i;
	db_qsort(a, 257, sizeof a[0], cmp_int);
	check(sorted(a, 257, sizeof a[0], cmp_int) && a[0] == 0 && a[256] == 256,
	    "reversed");
	for (i = 0; i < 257; i++)
		a[i] = (int)(i % 3);
	db_qsort(a, 257, sizeof a[0], cmp_int);
	check(sorted(a, 257, sizeof a[0], cmp_int), "three distinct keys");

	/* A zero record size or a count under two is a no-op. */
	a[0] = 5; a[1] = 3;
	db_qsort(a, 2, 0, cmp_int);
	check(a[0] == 5 && a[1] == 3, "size 0 sorts nothing");
	db_qsort(a, 1, sizeof a[0], cmp_int);
	db_qsort(a, 0, sizeof a[0], cmp_int);
	check(a[0] == 5 && a[1] == 3, "n of 1 and 0 sort nothing");
}

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "nested") == 0)
		nested_sort();
	else {
		sizes_and_counts();
		shapes();
		nested_sort();
	}
	if (failures) {
		fprintf(stderr, "qsort test: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("qsort test: %d checks passed\n", checks);
	return 0;
}
