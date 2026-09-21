/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include "SYS.h"

ENTRY(ptrace)
	/* ptrace may return -1 on success, so errno carries the error verdict. */
	ldr	r1, =errno
	mov	r2, #0
	str	r2, [r1]
	svc	#SYS_ptrace
	bcs	1f
	bx	lr
1:	ldr	r1, =errno
	str	r0, [r1]
	mov	r0, #0
	mvn	r0, r0
	bx	lr
END(ptrace)
