#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <err.h>

char *__progname;

static void
forward(int mode, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    switch (mode) {
    case 4: verr(23, fmt, ap); break;
    case 5: verrx(24, fmt, ap); break;
    case 6: vwarn(fmt, ap); break;
    case 7: vwarnx(fmt, ap); break;
    }
    va_end(ap);
}

int
main(int argc, char **argv)
{
    int mode = atoi(argv[1]);
    const char *fmt = argc > 2 ? NULL : "%d %s %.2f";

    __progname = "err-probe";
    errno = ENOENT;
    switch (mode) {
    case 0: err(21, fmt, 37, "word", 1.25); break;
    case 1: errx(22, fmt, 37, "word", 1.25); break;
    case 2: warn(fmt, 37, "word", 1.25); break;
    case 3: warnx(fmt, 37, "word", 1.25); break;
    default: forward(mode, fmt, 37, "word", 1.25); break;
    }
    /* Returning warnings must leave their caller able to continue. */
    return 19;
}
