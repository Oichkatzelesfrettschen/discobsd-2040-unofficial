#include <limits.h>
#include <stdlib.h>

/*
 * C17 7.22.7.3.  The C-locale encoding of mbtowc.c represents the wide
 * values 0 to UCHAR_MAX, one byte each; any other value has no multibyte
 * form.  The cast through unsigned long sends a negative value of a signed
 * wchar_t above UCHAR_MAX as well.
 */
int
wctomb(char *s, wchar_t wc)
{
	if (s == NULL)
		return 0;
	if ((unsigned long)wc > UCHAR_MAX)
		return -1;
	*s = (char)wc;
	return 1;
}
