/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * preempt_regs(misalign): load a sentinel into r1-r12 and lr and compare
 * every one in a loop that makes no system call, until preempt_done turns
 * nonzero (the SIGALRM handler of preempt.c sets it) or an iteration cap
 * runs out. r0 is the only scratch register. misalign zero runs the loop
 * with sp 8-byte aligned, nonzero with sp 4 modulo 8, so the exception
 * entries that interrupt it stack their frames with and without the
 * STKALIGN pad word (ARMv6-M ARM B1.5.6).
 *
 * Returns 0 when preempt_done was seen with every sentinel intact, the
 * number of the first register found changed (14 for lr), or 99 when the
 * cap ran out first.
 */
	.syntax	unified
	.thumb
	.text

	.equ	CAP, 0x02000000

	.macro	sentinel reg, value
	ldr	r0, =\value
	mov	\reg, r0
	.endm

	.macro	expect reg, value, number
	ldr	r0, =\value
	cmp	r0, \reg
	beq	9f
	movs	r0, #\number
	b	out
9:
	.endm

	.align	2
	.globl	preempt_regs
	.type	preempt_regs, %function
	.thumb_func
preempt_regs:
	push	{r4-r7, lr}
	mov	r1, r8
	mov	r2, r9
	mov	r3, r10
	mov	r4, r11
	push	{r1-r4}
	ldr	r1, =preempt_saved_sp
	mov	r2, sp
	str	r2, [r1]
	/* Nine words pushed: sp is 4 modulo 8 here and after the 8 below. */
	sub	sp, #8
	cmp	r0, #0
	bne	1f
	sub	sp, #4
1:	ldr	r1, =CAP
	str	r1, [sp]
	sentinel r8, 0x88888888
	sentinel r9, 0x99999999
	sentinel r10, 0xaaaaaaaa
	sentinel r11, 0xbbbbbbbb
	sentinel ip, 0xcccccccc
	sentinel lr, 0xeeeeeeef
	ldr	r1, =0x11111111
	ldr	r2, =0x22222222
	ldr	r3, =0x33333333
	ldr	r4, =0x44444444
	ldr	r5, =0x55555555
	ldr	r6, =0x66666666
	ldr	r7, =0x77777777
	b	loop
	.ltorg

loop:
	expect	r1, 0x11111111, 1
	expect	r2, 0x22222222, 2
	expect	r3, 0x33333333, 3
	expect	r4, 0x44444444, 4
	expect	r5, 0x55555555, 5
	expect	r6, 0x66666666, 6
	expect	r7, 0x77777777, 7
	expect	r8, 0x88888888, 8
	expect	r9, 0x99999999, 9
	expect	r10, 0xaaaaaaaa, 10
	expect	r11, 0xbbbbbbbb, 11
	expect	ip, 0xcccccccc, 12
	expect	lr, 0xeeeeeeef, 14
	ldr	r0, =preempt_done
	ldr	r0, [r0]
	cmp	r0, #0
	bne	done
	ldr	r0, [sp]
	subs	r0, #1
	str	r0, [sp]
	beq	cap
	b	loop
	.ltorg

done:
	movs	r0, #0
	b	out
cap:
	movs	r0, #99
out:
	ldr	r1, =preempt_saved_sp
	ldr	r1, [r1]
	mov	sp, r1
	pop	{r1-r4}
	mov	r8, r1
	mov	r9, r2
	mov	r10, r3
	mov	r11, r4
	pop	{r4-r7, pc}
	.ltorg
	.size	preempt_regs, . - preempt_regs
