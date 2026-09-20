#include <stddef.h>
#include <stdio.h>

void db_explicit_bzero(void *, size_t);
int db_timingsafe_bcmp(const void *, const void *, size_t);

static int
check(int condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "libc string security test: %s\n", message);
	return 1;
}

int
main(void)
{
	unsigned char first[256];
	unsigned char second[256];
	unsigned int index;
	int failures = 0;

	for (index = 0; index < sizeof(first); index++) {
		first[index] = (unsigned char)index;
		second[index] = (unsigned char)index;
	}
	db_explicit_bzero(first, 0);
	failures += check(first[0] == 0 && first[255] == 255,
	    "a zero-length erase changes no byte");
	db_explicit_bzero(first + 17, 223);
	for (index = 0; index < sizeof(first); index++) {
		unsigned char expected = index >= 17 && index < 240 ?
		    0 : (unsigned char)index;

		failures += check(first[index] == expected,
		    "a partial erase changes the wrong byte");
	}
	db_explicit_bzero(first, sizeof(first));
	for (index = 0; index < sizeof(first); index++)
		failures += check(first[index] == 0,
		    "a full erase leaves a byte behind");

	failures += check(db_timingsafe_bcmp(NULL, NULL, 0) == 0,
	    "zero-length inputs differ");
	failures += check(db_timingsafe_bcmp(second, second,
	    sizeof(second)) == 0, "equal buffers differ");
	for (index = 0; index < sizeof(second); index++)
		first[index] = second[index];
	first[0] ^= 1;
	failures += check(db_timingsafe_bcmp(first, second,
	    sizeof(first)) != 0, "a first-byte difference compares equal");
	first[0] = second[0];
	first[127] ^= 1;
	failures += check(db_timingsafe_bcmp(first, second,
	    sizeof(first)) != 0, "a middle-byte difference compares equal");
	first[127] = second[127];
	first[255] ^= 1;
	failures += check(db_timingsafe_bcmp(first, second,
	    sizeof(first)) != 0, "a final-byte difference compares equal");

	if (failures == 0)
		printf("libc string security tests passed\n");
	return failures != 0;
}
