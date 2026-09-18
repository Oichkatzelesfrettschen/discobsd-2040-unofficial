/*
 * Forced into the compilation of sys/kern/sys_generic.c for rwuio_test.
 *
 * rwuio() calls setjmp(&u.u_qsave) and sleep() longjmps back to it, and
 * setjmp is the port's assembly routine. The gate resolves that call to
 * the host C library's setjmp, captured in rwuio's own activation, over a
 * host jmp_buf that rwuio_jump.c owns; the file operation stub answers
 * through hk_qsave_longjmp() while that activation is live. The library
 * pair is what every host implements for its own target, where clang 17
 * refuses __builtin_setjmp on arm64 while __has_builtin reports it
 * available, so a builtin path needs a compilation probe and this gate
 * carries none.
 *
 * The host's <setjmp.h> cannot be included here: under the kernel's
 * include paths its <sys/cdefs.h> and the machine headers resolve to the
 * kernel's. sys/types.h declares int setjmp(label_t *), which names the
 * same symbol the host library exports with a pointer parameter, since a
 * jmp_buf is an array and decays to a pointer at the call; that
 * declaration stands, gains returns_twice so the compiler treats the call
 * as the host header would, and the macro below rewrites only the call so
 * the buffer it receives is the host's, never the kernel's label_t.
 */
#include <sys/types.h>

int setjmp(label_t *) __attribute__((returns_twice));
void *hk_qsave_jmpbuf(const void *env);
#define setjmp(env)	setjmp((label_t *)hk_qsave_jmpbuf(env))
