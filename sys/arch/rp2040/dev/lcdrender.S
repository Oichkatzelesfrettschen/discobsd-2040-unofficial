/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
