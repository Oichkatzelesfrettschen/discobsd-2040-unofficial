/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_f2iz)
	lsls	r1, r0, #1
	lsrs	r2, r1, #24
	movs	r3, #0x80
	lsls	r3, r3, #24
	cmp	r2, #126
	ble	Lf2iz_zero
	subs	r2, #158
	bge	Lf2iz_overflow
	asrs	r1, r0, #31
	lsls	r0, r0, #9
	lsrs	r0, r0, #1
	orrs	r0, r3
	negs	r2, r2
	lsrs	r0, r0, r2
	lsls	r1, r1, #1
	adds	r1, #1
	muls	r0, r1
	bx	lr
Lf2iz_zero:
	movs	r0, #0
	bx	lr
Lf2iz_overflow:
	lsrs	r0, r0, #31
	adds	r0, r0, r3
	subs	r0, #1
	bx	lr
ROM_FLOAT_FUNCTION_END(__aeabi_f2iz)
ROM_FLOAT_ALIAS(__fixsfsi, __aeabi_f2iz)
