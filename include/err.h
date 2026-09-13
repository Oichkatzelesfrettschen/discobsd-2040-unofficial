/*
 * Error and warning reporting, the 4.4BSD err(3) family.
 *
 * The eight functions declared here are defined by lib/libc/gen/err.c.
 * Each prefixes its message with __progname; the err and errx forms
 * exit with the given status, the warn and warnx forms return. The
 * non-x forms append strerror(errno).
 */
#ifndef _ERR_H_
#define _ERR_H_

/*
 * The v-forms take a va_list, and a caller of verr() needs the type to
 * declare its own argument, so <err.h> defines it rather than borrowing
 * a name it would then undefine.
 */
#include <stdarg.h>

void    err (int eval, const char *fmt, ...);
void    errx (int eval, const char *fmt, ...);
void    warn (const char *fmt, ...);
void    warnx (const char *fmt, ...);
void    verr (int eval, const char *fmt, va_list ap);
void    verrx (int eval, const char *fmt, va_list ap);
void    vwarn (const char *fmt, va_list ap);
void    vwarnx (const char *fmt, va_list ap);

#endif /* !_ERR_H_ */
