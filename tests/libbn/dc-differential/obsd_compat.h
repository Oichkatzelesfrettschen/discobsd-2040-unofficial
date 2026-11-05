#ifndef OBSD_COMPAT_H
#define OBSD_COMPAT_H
#define _GNU_SOURCE
#include <stdlib.h>
#define __dead __attribute__((__noreturn__))
static inline int pledge(const char *p, const char *e) { (void)p; (void)e; return 0; }
#endif
#ifndef OBSD_COMPAT_TAIL
#define OBSD_COMPAT_TAIL
#include <stdio.h>
#include <errno.h>
#include <err.h>
static int optreset;
#define errc(eval, code, ...) do { errno = (code); err((eval), __VA_ARGS__); } while (0)
#endif
#include <unistd.h>
#define dup dc_dup
#define setproctitle(...) ((void)0)
