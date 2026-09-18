/*
 * The jump buffer behind rwuio_setjmp.h. This unit sees the host's
 * <setjmp.h> and nothing of the kernel's, so the buffer has the host's
 * type and alignment, and the two sides meet on the label_t address alone:
 * the capture records which label_t it stood for, and the jump refuses any
 * other, since a longjmp to a buffer nobody captured is undefined. The
 * buffer crosses to the kernel unit as void *, which the macro there
 * hands to the host's setjmp through the kernel's pointer-parameter
 * declaration of it.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#include "rwuio_jump.h"

void *hk_qsave_jmpbuf(const void *env);

static jmp_buf qsave;
static const void *qsave_env;

void *
hk_qsave_jmpbuf(const void *env)
{
	qsave_env = env;
	return qsave;
}

void
hk_qsave_longjmp(const void *env, int value)
{
	if (env == NULL || env != qsave_env) {
		fprintf(stderr, "rwuio: longjmp to %p, but setjmp captured %p\n",
		    env, qsave_env);
		abort();
	}
	longjmp(qsave, value);
}
