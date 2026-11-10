#include <stdlib.h>

/*
 * C17 7.22.7.2.  In the C locale each byte is one character whose wide
 * value is the byte read as unsigned char, so bytes 0x80 to 0xff map to
 * the wide values of the same number and no byte is invalid.
 */
int
mbtowc(wchar_t *pwc, const char *s, size_t n)
{
	if (s == NULL)
		return 0;
	if (n == 0)
		return -1;
	if (pwc != NULL)
		*pwc = (wchar_t)(unsigned char)*s;
	return *s != '\0';
}
