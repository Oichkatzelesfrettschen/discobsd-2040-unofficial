#include <stdio.h>
#include <stdarg.h>

int
printf(const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vfprintf(stdout, format, arguments);
	va_end(arguments);
	return result;
}
