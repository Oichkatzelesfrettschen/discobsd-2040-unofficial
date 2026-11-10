#ifndef _SETJMP_H
#define _SETJMP_H

#include <sys/cdefs.h>
#ifndef __dead
/* A hosted test can reach the host's <sys/cdefs.h>, which has no __dead. */
#define __dead		_Noreturn
#endif
#include <machine/setjmp.h>

typedef int jmp_buf[_JBLEN];
typedef jmp_buf sigjmp_buf;

/*
 * Save and restore only CPU state.
 * Signal mask is not saved.
 */
int _setjmp (jmp_buf env);
__dead void _longjmp (jmp_buf env, int val);

/*
 * Save and restore CPU state and signal mask.
 */
int setjmp (jmp_buf env);
__dead void longjmp (jmp_buf env, int val);

/*
 * Save and restore CPU state and optionally a signal mask.
 * Signal mask is saved only when savesigs is nonzero.
 */
int sigsetjmp (sigjmp_buf env, int savesigs);
__dead void siglongjmp (sigjmp_buf env, int val);

#endif /* !_SETJMP_H */
