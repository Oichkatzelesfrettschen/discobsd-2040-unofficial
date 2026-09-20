#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int
scanf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = _doscan(stdin, fmt, ap);
	va_end(ap);
	return n;
}

int
fscanf(FILE *fp, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = _doscan(fp, fmt, ap);
	va_end(ap);
	return n;
}

/*
 * The read function of a string stream: the string is the whole buffer,
 * so a refill finds end of input. The scanner pushes back only the
 * character it has just read, which ungetc satisfies by stepping _p back
 * or through the stream's own _ubuf, so this shared block is never
 * written to and no per-call fileops allocation happens.
 */
static int
eofread(void *cookie, char *buf, int n)
{
	(void)cookie;
	(void)buf;
	(void)n;
	return 0;
}

static struct __sfops eofops = {
	NULL, NULL, eofread, NULL, NULL, { NULL, 0 }, { NULL, 0 }
};

int
vfscanf(FILE *fp, const char *fmt, va_list ap)
{
	return _doscan(fp, fmt, ap);
}

int
vscanf(const char *fmt, va_list ap)
{
	return _doscan(stdin, fmt, ap);
}

int
vsscanf(const char *str, const char *fmt, va_list ap)
{
	FILE f;

	f._flags = __SRD | __SSTR;
	f._p = f._bf._base = (unsigned char *)str;
	f._r = f._bf._size = (int)strlen(str);
	f._w = 0;
	f._lbfsize = 0;
	f._file = -1;
	f._fops = &eofops;
	f._up = NULL;
	f._ur = 0;
	return _doscan(&f, fmt, ap);
}

int
sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsscanf(str, fmt, ap);
	va_end(ap);
	return n;
}
