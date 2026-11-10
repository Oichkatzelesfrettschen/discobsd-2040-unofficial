#include <stdlib.h>

/*
 * C17 7.22.7.1.  The library implements the C locale alone, whose multibyte
 * encoding is one byte per character with no shift state, so a null s
 * reports a stateless encoding and every byte is a whole character.
 */
int
mblen(const char *s, size_t n)
{
	if (s == NULL)
		return 0;
	if (n == 0)
		return -1;
	return *s != '\0';
}
