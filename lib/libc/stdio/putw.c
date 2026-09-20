#include <stdio.h>

int
putw(int w, FILE *iop)
{
	const char *p = (const char *)&w;
	int i;

	for (i = sizeof(int); --i >= 0;)
		putc(*p++, iop);
	return (ferror(iop));
}
