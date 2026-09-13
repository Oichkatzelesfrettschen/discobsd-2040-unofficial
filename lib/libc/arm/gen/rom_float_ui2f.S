/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_ui2f)
	movs	r1, #0
	cmp	r0, #0
	beq	Lui2f_zero
	push	{r0, r1, r4, lr}
	ldr	r3, =ROM_FLOAT_CELL_NAME(clz)
	ldr	r3, [r3]
	blx	r3
	pop	{r1, r2}
	lsls	r1, r1, r0
	subs	r0, #158
	negs	r0, r0
	adds	r1, #0x80
	bcs	Lui2f_round_carry
	lsls	r3, r1, #24
	beq	Lui2f_round_tie
	lsls	r1, r1, #1
Lui2f_pack:
	lsrs	r1, r1, #9
	lsls	r0, r0, #23
	orrs	r0, r2
	orrs	r0, r1
	pop	{r4, pc}
Lui2f_round_carry:
	adds	r0, #1
	b	Lui2f_pack
Lui2f_round_tie:
	lsrs	r1, r1, #9
	lsls	r1, r1, #10
	b	Lui2f_pack
Lui2f_zero:
	mov	r0, r1
	bx	lr
	.pool
ROM_FLOAT_FUNCTION_END(__aeabi_ui2f)
ROM_FLOAT_ALIAS(__floatunsisf, __aeabi_ui2f)
