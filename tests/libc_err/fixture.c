#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "err.h"

char *__progname;

static void
forward(int mode, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    switch (mode) {
    case 0: vwarn(format, args); break;
    case 1: vwarnx(format, args); break;
    case 2: verr(23, format, args); break;
    case 3: verrx(23, format, args); break;
    }
    va_end(args);
}

int
main(int argc, char **argv)
{
    int mode, indirect;
    const char *format;

    if (argc != 4) return 2;
    mode = atoi(argv[1]);
    indirect = atoi(argv[2]);
    format = atoi(argv[3]) ? "%d %s %.2f" : NULL;
    __progname = "err-fixture";
    errno = ENOENT;
    if (indirect) {
        forward(mode, format, 17, "value", 1.25);
    } else {
        switch (mode) {
        case 0: warn(format, 17, "value", 1.25); break;
        case 1: warnx(format, 17, "value", 1.25); break;
        case 2: err(23, format, 17, "value", 1.25); break;
        case 3: errx(23, format, 17, "value", 1.25); break;
        }
    }
    puts("returned");
    return 0;
}
