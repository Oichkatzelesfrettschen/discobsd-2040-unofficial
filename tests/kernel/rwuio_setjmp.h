/*
 * Redirect rwuio's checkpoint to the host C library for rwuio_test.
 * sys/types.h supplies the returns-twice declaration for the symbol
 * that takes a buffer pointer. The macro supplies rwuio_jump.c's host
 * jmp_buf while setjmp still runs in rwuio's own activation. The file
 * operation stub resumes it through hk_qsave_longjmp().
 */
#include <sys/types.h>

void *hk_qsave_jmpbuf(const void *env);
#define setjmp(env)	setjmp((label_t *)hk_qsave_jmpbuf(env))
