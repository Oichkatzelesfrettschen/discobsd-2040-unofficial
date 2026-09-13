/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_i2d)
	asrs	r1, r0, #31
	eors	r0, r1
	subs	r0, r0, r1
	beq	Li2d_zero
	lsls	r1, r1, #31
	push	{r0, r1, r4, lr}
	ldr	r3, =ROM_FLOAT_CELL_NAME(clz)
	ldr	r3, [r3]
	blx	r3
	pop	{r2, r3}
	adds	r4, r0, #1
	lsls	r2, r2, r4
	lsls	r0, r2, #20
	lsrs	r2, r2, #12
	ldr	r1, =1055
	subs	r1, r1, r4
	lsls	r1, r1, #20
	orrs	r1, r3
	orrs	r1, r2
	pop	{r4, pc}
Li2d_zero:
	bx	lr
	.pool
ROM_FLOAT_FUNCTION_END(__aeabi_i2d)
ROM_FLOAT_ALIAS(__floatsidf, __aeabi_i2d)
