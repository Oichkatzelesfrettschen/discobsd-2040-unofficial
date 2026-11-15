#include <stdio.h>

int
fputc(int c, FILE *fp)
{
	return putc(c, fp);
}

int
(putc)(int c, FILE *fp)
{
	return fputc(c, fp);
}
