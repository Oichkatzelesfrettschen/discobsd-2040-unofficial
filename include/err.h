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
 * va_list without <stdarg.h>: the v-forms need the type name only, and
 * a caller that includes <stdarg.h> first already has the real one.
 */
#ifndef _VA_LIST_
# ifdef __GNUC__
#  define va_list   __builtin_va_list   /* For Gnu C */
# endif
# ifdef __SMALLER_C__
#  define va_list   char *              /* For Smaller C */
# endif
#endif

void    err (int eval, const char *fmt, ...);
void    errx (int eval, const char *fmt, ...);
void    warn (const char *fmt, ...);
void    warnx (const char *fmt, ...);
void    verr (int eval, const char *fmt, va_list ap);
void    verrx (int eval, const char *fmt, va_list ap);
void    vwarn (const char *fmt, va_list ap);
void    vwarnx (const char *fmt, va_list ap);

#ifndef _VA_LIST_
# undef va_list
#endif

#endif /* !_ERR_H_ */
