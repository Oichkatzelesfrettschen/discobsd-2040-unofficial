#include <stdio.h>

int
fgetc(FILE *fp)
{
	return getc(fp);
}

int
(getc)(FILE *fp)
{
	return fgetc(fp);
}
