/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * IRQ 26, the PicoCalc console renderer, on a stack of its own.
 *
 * ARMv6-M has one handler stack, MSP, and this kernel keeps it in the
 * current process's u-area, where the measured kernel-stack peak leaves
 * about 220 of 2,048 bytes (CAPACITY.md). The renderer is an extra
 * exception level, and SysTick, USB and a fault report can nest above
 * it, so it runs on lcd_render_sp instead: the hardware has already
 * stacked the exception frame on the interrupted stack, this entry saves
 * that SP, switches to the render stack, calls lcdrender(), restores the
 * saved SP and returns through EXC_RETURN, which unstacks the frame from
 * where the hardware put it. Every exception that preempts lcdrender()
 * stacks on the render stack. PendSV, which switches processes, is less
 * urgent than this interrupt and never runs on top of it.
 */

	.syntax	unified
	.cpu	cortex-m0plus
	.thumb

	.text
	.align	2
	.globl	SPARE_IRQ_0_Handler
	.type	SPARE_IRQ_0_Handler, %function
	.thumb_func
SPARE_IRQ_0_Handler:
	mov	r0, sp			/* The interrupted stack, frame on top. */
	ldr	r1, =lcd_render_sp
	ldr	r1, [r1]
	mov	sp, r1			/* 8-byte aligned top of the render stack. */
	push	{r0, lr}		/* Saved SP and EXC_RETURN; keeps alignment. */
	bl	lcdrender
	pop	{r0, r1}
	mov	sp, r0
	bx	r1			/* Exception return. */
	.size	SPARE_IRQ_0_Handler, . - SPARE_IRQ_0_Handler

	.ltorg
