#include <limits.h>
#include <stdlib.h>

/*
 * C17 7.22.8.2 over the C-locale encoding of wctomb.c.  At most n bytes are
 * stored, the terminating null among them when it fits, and the count
 * excludes it.  A wide value outside 0 to UCHAR_MAX before the limit makes
 * the result (size_t)-1.  A null s, which POSIX admits, counts the bytes
 * the conversion would store without the limit.
 */
size_t
wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
	size_t count;

	for (count = 0; s == NULL || count < n; count++) {
		if ((unsigned long)pwcs[count] > UCHAR_MAX)
			return (size_t)-1;
		if (s != NULL)
			s[count] = (char)pwcs[count];
		if (pwcs[count] == 0)
			break;
	}
	return count;
}
