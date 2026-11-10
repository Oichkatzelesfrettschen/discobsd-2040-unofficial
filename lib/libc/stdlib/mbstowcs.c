#include <stdlib.h>
#include <string.h>

/*
 * C17 7.22.8.1 over the C-locale encoding of mbtowc.c.  At most n wide
 * characters are stored, the terminating null among them when it fits, and
 * the count excludes it.  Every byte converts, so the error result never
 * occurs.  A null pwcs, which POSIX admits, counts the characters and
 * stores nothing.
 */
size_t
mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
	const unsigned char *cursor;
	size_t count;

	if (pwcs == NULL)
		return strlen(s);
	cursor = (const unsigned char *)s;
	for (count = 0; count < n; count++) {
		pwcs[count] = (wchar_t)cursor[count];
		if (cursor[count] == '\0')
			break;
	}
	return count;
}
