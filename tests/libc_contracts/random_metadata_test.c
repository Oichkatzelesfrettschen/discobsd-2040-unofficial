#include <errno.h>
#include <stdint.h>
#include <stdio.h>

char *db_initstate(unsigned seed, char *state, int size);
char *db_setstate(char *state);

int random_test_errno;

int
main(void)
{
	uint32_t active[32] = { 0 };
	uint32_t malformed[32] = { 315 };

	if (db_initstate(1, (char *)active, sizeof(active)) == NULL)
		return 1;
	random_test_errno = 0;
	if (db_setstate((char *)malformed) != NULL || random_test_errno != EINVAL) {
		fputs("malformed random state was installed\n", stderr);
		return 1;
	}
	return 0;
}
