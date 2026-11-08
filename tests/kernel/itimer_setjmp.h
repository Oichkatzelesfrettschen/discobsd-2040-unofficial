/*
 * Forced into the compilation of sys/kern/kern_fork.c for itimer_test.
 *
 * newproc() calls setjmp(&u.u_ssave) and takes the child's path only when
 * swapout() later resumes the saved context. The gate follows the parent's
 * path alone, so the call resolves to a stub that returns zero and records
 * no context. sys/types.h declares int setjmp(label_t *); the include comes
 * first so that declaration is the one the macro renames.
 */
#include <sys/types.h>

int hk_fork_setjmp(label_t *env);
#define setjmp(env)	hk_fork_setjmp(env)
