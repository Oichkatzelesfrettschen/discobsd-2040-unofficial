#include <stddef.h>

char *
db_initstate(unsigned seed, char *state, int size)
{
	(void)seed;
	(void)size;
	return state;
}

char *
db_setstate(char *state)
{
	return state;
}
