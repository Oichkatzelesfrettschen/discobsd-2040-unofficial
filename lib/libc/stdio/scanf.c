#include <stdio.h>
#include <stdarg.h>

static int
scan_string(const char *str, const char *fmt, va_list args)
{
	FILE string_stream;
	const char *cursor;
	int length;

	length = 0;
	for (cursor = str; *cursor != '\0'; cursor++)
		length++;
	string_stream._cnt = length;
	string_stream._ptr = string_stream._base = (char *)str;
	string_stream._bufsiz = length;
	string_stream._flag = _IOREAD | _IOSTRG;
	string_stream._file = -1;
	string_stream._ub[0] = 0;
	return _doscan(&string_stream, fmt, args);
}

int
scanf (const char *fmt, ...)
{
	va_list args;
	int n;

	va_start (args, fmt);
	n = _doscan (stdin, fmt, args);
	va_end (args);
	return n;
}

int
fscanf (FILE *iop, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start (args, fmt);
	n = _doscan(iop, fmt, args);
	va_end (args);
	return n;
}

int
sscanf (const char *str, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start (args, fmt);
	n = scan_string(str, fmt, args);
	va_end (args);
	return n;
}

int
vfscanf(FILE *iop, const char *fmt, va_list args)
{
	return _doscan(iop, fmt, args);
}

int
vscanf(const char *fmt, va_list args)
{
	return _doscan(stdin, fmt, args);
}

int
vsscanf(const char *str, const char *fmt, va_list args)
{
	return scan_string(str, fmt, args);
}
