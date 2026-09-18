/*
 * Forced into the compilation of sys/kern/sys_generic.c for rwuio_test.
 *
 * rwuio() calls setjmp(&u.u_qsave) and sleep() longjmps back to it, and
 * setjmp is the port's assembly routine. sys/types.h declares it as
 * int setjmp(label_t *), which this header lets stand by including the
 * declaration first; the macro below then rewrites only the call, into the
 * compiler's __builtin_setjmp, which the gate's file operation stub answers
 * with __builtin_longjmp while rwuio's frame is live. label_t is ten longs
 * and the builtin wants five words, at either width.
 */
#include <sys/types.h>
#define setjmp(env)	__builtin_setjmp((void **)(env))
