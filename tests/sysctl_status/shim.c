/* Host stdio stays on the host side of the ILP32 fixture boundary. */
#include <stdarg.h>
#include <stdio.h>
int status_print(int diagnostic, const char *format, va_list arguments)
{
    return vfprintf(diagnostic ? stderr : stdout, format, arguments);
}
