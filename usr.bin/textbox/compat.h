/*
 * Compatibility shim for the sbase (https://git.suckless.org/sbase)
 * utilities imported under usr.bin: cut, paste, seq, dirname, nl,
 * cksum, expand, unexpand, mkfifo, uuencode and uudecode. sbase's
 * own util.h pulls in regex_t and a POSIX getline() the tree's libc
 * does not carry, and its utf.h decodes real UTF-8; this header
 * declares a trimmed set: the eprintf/weprintf family, the alloc and
 * string helpers each tool actually calls, a byte-wide stand-in for
 * sbase's Rune so cut, paste, expand, unexpand and nl keep their
 * upstream logic on octet input, and a getline() the tools need but
 * this libc lacks. No entry point here parses or prints a float.
 */
#ifndef TEXTBOX_COMPAT_H
#define TEXTBOX_COMPAT_H

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "arg.h"

/* This libc's limits.h defines SIZE_T_MAX and no SIZE_MAX; a host
 * built against a C99 stdint.h has SIZE_MAX and no SIZE_T_MAX. */
#if !defined(SIZE_T_MAX) && defined(SIZE_MAX)
#define SIZE_T_MAX SIZE_MAX
#endif

#undef MIN
#define MIN(x,y)  ((x) < (y) ? (x) : (y))
#undef MAX
#define MAX(x,y)  ((x) > (y) ? (x) : (y))

extern char *argv0;

void  eprintf(const char *, ...) __attribute__((noreturn));
void  weprintf(const char *, ...);
void  xvprintf(const char *, va_list);

void *emalloc(size_t);
char *estrdup(const char *);

void *reallocarray(void *, size_t, size_t);
void *ereallocarray(void *, size_t, size_t);

int    fshut(FILE *, const char *);

long   strtonum(const char *, long, long, const char **);
long   estrtonum(const char *, long, long);

size_t unescape(char *);

/* strlcpy(3) and strlcat(3) are in this libc's string.h already; the
 * "e" spellings sbase calls are the same functions without the
 * separate out-of-memory-style abort sbase gives a truncated copy. */
#define estrlcpy(d, s, n)  strlcpy((d), (s), (n))
#define estrlcat(d, s, n)  strlcat((d), (s), (n))

/* Every byte starts a rune when a rune is a byte. */
#define UTF8_POINT(c)  1

void  *memmem(const void *, size_t, const void *, size_t);

mode_t parsemode(const char *, mode_t, mode_t);

ssize_t getline(char **, size_t *, FILE *);

/*
 * A byte is a rune: the console this port targets is ASCII, and a
 * one-byte "rune" keeps cut, paste, expand, unexpand and nl's control
 * flow unchanged while dropping libutf's decode tables and multibyte
 * state.
 */
typedef unsigned char Rune;
enum { UTFmax = 1 };

int    fullrune(const char *, size_t);
size_t utflen(const char *);

int    fgetrune(Rune *, FILE *);
int    efgetrune(Rune *, FILE *, const char *);
int    fputrune(const Rune *, FILE *);
int    efputrune(const Rune *, FILE *, const char *);

#endif
