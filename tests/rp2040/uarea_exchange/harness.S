/*
 * Host harness for the u-area exchange loop in arch/rp2040/rp2040/locore.S.
 *
 * The Makefile copies the lines between the uarea-exchange markers of
 * locore.S into loop.S, so the instructions under test are the kernel's
 * own. The harness fills two USIZE buffers with distinct patterns, runs
 * the loop under its register contract (r0 walks u, r1 walks u0, r8 is
 * the end of u), then checks that every byte of each buffer now holds
 * the other's pattern and that r0 and r1 stopped exactly at their ends.
 * It is a static Linux binary for qemu-arm: the only system call is
 * exit, whose status is 0 on success, 1 on a byte mismatch, 2 when a
 * pointer overran, so the Makefile reads the verdict from $?.
 */
#define USIZE 3072

	.syntax unified
	.thumb
	.text
	.align	2
	.globl	_start
	.thumb_func
_start:
	/* u[i] = i & 0xff, u0[i] = ~i & 0xff. */
	ldr	r0, =ubuf
	ldr	r1, =u0buf
	ldr	r2, =USIZE
	movs	r3, #0
fill:
	strb	r3, [r0, r3]
	mvns	r4, r3
	strb	r4, [r1, r3]
	adds	r3, #1
	cmp	r3, r2
	bne	fill

	/* The register contract longjmp establishes before the loop. */
	ldr	r0, =ubuf
	ldr	r1, =u0buf
	ldr	r2, =(ubuf + USIZE)
	mov	r8, r2

#include "loop.S"

	/* r0 and r1 must have stopped one byte past each buffer. */
	ldr	r2, =(ubuf + USIZE)
	cmp	r0, r2
	bne	overrun
	ldr	r2, =(u0buf + USIZE)
	cmp	r1, r2
	bne	overrun

	/* Now u[i] must be ~i & 0xff and u0[i] must be i & 0xff. */
	ldr	r0, =ubuf
	ldr	r1, =u0buf
	ldr	r2, =USIZE
	movs	r3, #0
	movs	r6, #0xff
check:
	ldrb	r4, [r0, r3]
	mvns	r5, r3
	ands	r5, r6
	cmp	r4, r5
	bne	mismatch
	ldrb	r4, [r1, r3]
	movs	r5, r3
	ands	r5, r6
	cmp	r4, r5
	bne	mismatch
	adds	r3, #1
	cmp	r3, r2
	bne	check
	movs	r0, #0
	b	exit
mismatch:
	movs	r0, #1
	b	exit
overrun:
	movs	r0, #2
exit:
	movs	r7, #1			/* Linux ARM EABI exit. */
	svc	#0
	.pool

	.bss
	.align	2
ubuf:	.space	USIZE
u0buf:	.space	USIZE
