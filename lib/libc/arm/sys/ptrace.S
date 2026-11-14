/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include "SYS.h"

ENTRY(ptrace)
	/* ptrace may return -1 on success, so errno carries the error verdict. */
	push	{r0-r3}
	SYS_ERRNO_ADDRESS
	mov	r2, #0
	str	r2, [r1]
	pop	{r0-r3}
	svc	#SYS_ptrace
	bcs	1f
	bx	lr
1:	SYS_ERROR
END(ptrace)
