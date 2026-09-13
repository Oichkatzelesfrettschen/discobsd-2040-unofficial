/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_d2iz)
	push	{r4, lr}
	lsls	r4, r1, #1
	lsrs	r2, r4, #21
	movs	r3, #0x80
	adds	r2, r2, r3
	lsls	r3, r3, #3
	subs	r2, r2, r3
	lsls	r3, r3, #21
	cmp	r2, #126
	ble	Ld2iz_zero
	subs	r2, #158
	bge	Ld2iz_overflow
	asrs	r4, r1, #31
	lsls	r1, r1, #12
	lsrs	r1, r1, #1
	orrs	r1, r3
	negs	r2, r2
	lsrs	r1, r1, r2
	lsls	r4, r4, #1
	adds	r4, #1
	adds	r2, #21
	cmp	r2, #32
	bge	Ld2iz_high
	lsrs	r0, r0, r2
	orrs	r0, r1
	muls	r0, r4
	pop	{r4, pc}
Ld2iz_zero:
	movs	r0, #0
	pop	{r4, pc}
Ld2iz_high:
	mov	r0, r1
	muls	r0, r4
	pop	{r4, pc}
Ld2iz_overflow:
	lsrs	r0, r1, #31
	adds	r0, r0, r3
	subs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_d2iz)
ROM_FLOAT_ALIAS(__fixdfsi, __aeabi_d2iz)
