#include <stddef.h>
#include <stdio.h>

void *db_memmem(const void *, size_t, const void *, size_t);

static int
check(int condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "memmem contract: %s\n", message);
	return 1;
}

int
main(void)
{
	static const unsigned char haystack[] = {
		'a', 0, 'b', 'c', 'a', 0, 'b'
	};
	static const unsigned char embedded[] = { 'a', 0, 'b' };
	static const unsigned char boundary[] = { 'c', 'a', 0, 'b' };
	static const unsigned char absent[] = { 'b', 0, 'a' };
	int failures = 0;

	failures += check(db_memmem(haystack, sizeof(haystack), embedded,
	    sizeof(embedded)) == haystack, "embedded NUL bytes participate in a match");
	failures += check(db_memmem(haystack, sizeof(haystack), boundary,
	    sizeof(boundary)) == haystack + 3, "the final possible position is searched");
	failures += check(db_memmem(haystack, sizeof(haystack), absent,
	    sizeof(absent)) == NULL, "an absent needle returns NULL");
	failures += check(db_memmem(haystack, 2, embedded, sizeof(embedded)) == NULL,
	    "a needle longer than the haystack returns NULL");
	failures += check(db_memmem(haystack, sizeof(haystack), embedded, 0) ==
	    haystack, "an empty needle returns the haystack");
	failures += check(db_memmem(haystack, sizeof(haystack), haystack + 1, 1) ==
	    haystack + 1, "a one-byte NUL needle is found");

	if (failures == 0)
		printf("memmem contract tests passed\n");
	return failures != 0;
}
