/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
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

#include <sys/proc_asm.h>

#define AREA_BYTES 3072
#define LABEL_OFFSET 32
#define LABEL_BYTES 40

	.syntax unified
	.thumb
	.text

	.macro require cond, status
	b\cond	99f
	movs	r0, #\status
	b	done
99:
	.endm

	.macro seed_regs base
	.irp n,4,5,6,7,8,9,10,11
	movs	r2, #(\base + \n)
	mov	r\n, r2
	.endr
	.endm

	.macro check_regs base, status
	.irp n,4,5,6,7,8,9,10,11
	mov	r2, r\n
	cmp	r2, #(\base + \n)
	require	eq, \status
	.endr
	.endm

	.macro check_sp status
	ldr	r2, =entry_sp
	ldr	r2, [r2]
	cmp	sp, r2
	require	eq, \status
	.endm

	.globl _start
	.thumb_func
_start:
	ldr	r3, =entry_sp
	mov	r2, sp
	str	r2, [r3]
	lsls	r2, r2, #29
	require	eq, 1

	seed_regs 16
	ldr	r0, =(u + LABEL_OFFSET)
	bl	setjmp
same_return:
	cmp	r0, #0
	bne	same_resumed
	check_regs 16, 2
	ldr	r0, =(u + LABEL_OFFSET)
	.irp n,4,5,6,7,8,9,10,11
	ldr	r2, [r0, #((\n - 4) * 4)]
	cmp	r2, #(16 + \n)
	require	eq, 3
	.endr
	ldr	r2, [r0, #32]
	cmp	sp, r2
	require	eq, 3
	ldr	r2, [r0, #36]
	ldr	r3, =(same_return + 1)
	cmp	r2, r3
	require	eq, 3

	seed_regs 0
	sub	sp, #16
	ldr	r0, =u
	ldr	r1, =(u + LABEL_OFFSET)
	bl	longjmp
	b	bad_return

same_resumed:
	cmp	r0, #1
	require	eq, 4
	check_regs 16, 5
	check_sp 5
	ldr	r0, =u
	ldr	r2, [r0]
	ldr	r3, =active_proc
	cmp	r2, r3
	require	eq, 6

	seed_regs 32
	ldr	r0, =(u + LABEL_OFFSET)
	bl	setjmp
	cmp	r0, #0
	bne	exchange_resumed
	check_regs 32, 2
	ldr	r0, =(u + LABEL_OFFSET)
	ldr	r1, =(u0 + LABEL_OFFSET)
	movs	r2, #0
	movs	r4, #0
1:
	ldr	r3, [r0, r2]
	str	r3, [r1, r2]
	str	r4, [r0, r2]
	adds	r2, #4
	cmp	r2, #LABEL_BYTES
	bne	1b

	seed_regs 0
	sub	sp, #16
	ldr	r0, =u0
	ldr	r1, =(u + LABEL_OFFSET)
	bl	resume
	b	bad_return

exchange_resumed:
	cmp	r0, #1
	require	eq, 4
	check_regs 32, 5
	check_sp 5
	ldr	r0, =u
	ldr	r1, =u0
	ldr	r2, [r0]
	ldr	r3, =incoming_proc
	cmp	r2, r3
	require	eq, 6
	ldr	r2, [r3, #P_ADDR_OFFSET]
	cmp	r2, r0
	require	eq, 6
	ldr	r2, [r1]
	ldr	r3, =active_proc
	cmp	r2, r3
	require	eq, 6
	ldr	r2, [r3, #P_ADDR_OFFSET]
	cmp	r2, r1
	require	eq, 6
	ldr	r2, =(u + AREA_BYTES - 4)
	ldr	r3, [r2]
	cmp	r3, #2
	require	eq, 7
	ldr	r2, =(u0 + AREA_BYTES - 4)
	ldr	r3, [r2]
	cmp	r3, #1
	require	eq, 7
	movs	r0, #0
	b	done
bad_return:
	movs	r0, #8
done:
	movs	r7, #1
	svc	#0
	.pool

	.data
	.balign 8
	.globl u, u0
u:	.word active_proc
	.space AREA_BYTES - 8
	.word 1
u0:	.word incoming_proc
	.space AREA_BYTES - 8
	.word 2
active_proc:
	.space P_ADDR_OFFSET
	.word u
incoming_proc:
	.space P_ADDR_OFFSET
	.word u0
entry_sp:
	.word 0
