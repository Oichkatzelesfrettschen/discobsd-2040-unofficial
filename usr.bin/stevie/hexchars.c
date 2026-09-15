/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Public domain (Unlicense); see LICENSE in this directory.
 *
 * Readable spellings for the bytes that have no glyph. Upstream carried
 * a 256-entry initialized table whose ch_str fields pointed at the
 * string literal "[xxx]", and hexchars/octchars/decchars sprintf'd over
 * that literal -- a write into read-only data that faults before the
 * first screen paint on any target that protects .rodata. The table and
 * its strings are built at run time here instead, which also keeps the
 * initializer and its relocations out of the a.out.
 *
 * The spelling rule reproduces the upstream table exactly: one column
 * for ASCII space through '~' and for '\n' and '\r', a five-column
 * "[xNN]" for every other byte.
 */

#include <stdio.h>
#include "stevie.h"

struct charinfo chars[256];
static char charstr[256][6];

static void
setchars(int base)
{
	int n;

	for (n = 0; n < 256; n++) {
		if ((n >= ' ' && n < 0177) || n == '\n' || n == '\r') {
			chars[n].ch_size = 1;
			chars[n].ch_str = NULL;
			continue;
		}
		switch (base) {
		case 8:
			sprintf(charstr[n], "[%03o]", n);
			break;
		case 10:
			sprintf(charstr[n], "[%3d]", n);
			break;
		default:
			sprintf(charstr[n], "[x%02x]", n);
			break;
		}
		chars[n].ch_size = 5;
		chars[n].ch_str = charstr[n];
	}
}

void
octchars(void)
{
	setchars(8);
}

void
hexchars(void)
{
	setchars(16);
}

void
decchars(void)
{
	setchars(10);
}

int
hextoint(int c)
{
	if ( c>='0' && c<='9' )
		return(c-'0');
	if ( c>='a' && c<='f' )
		return(10+c-'a');
	if ( c>='A' && c<='F' )
		return(10+c-'A');
	return(-1);
}
