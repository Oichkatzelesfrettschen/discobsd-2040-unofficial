/*
 * Integer division for ARMv6-M, which has no divide instruction. GCC and
 * the tree's Smaller C back end emit calls to the AEABI helpers below; GCC
 * finds them in libgcc, which the board does not carry, so libc provides
 * them. __aeabi_uidivmod returns the quotient in r0 and the remainder in
 * r1, and __aeabi_uidiv is the same entry with the remainder ignored; the
 * signed pair works on magnitudes and restores the signs afterwards: the
 * quotient is negative when the operand signs differ, the remainder takes
 * the dividend's sign (C99 6.5.5). Division by zero returns a zero
 * quotient and the dividend as remainder.
 */
	.syntax	unified
	.thumb
	.text

	.thumb_func
	.globl	__aeabi_uidiv
	.globl	__aeabi_uidivmod
	.type	__aeabi_uidiv, %function
	.type	__aeabi_uidivmod, %function
__aeabi_uidiv:
__aeabi_uidivmod:
	push	{r4, lr}
	movs	r2, #0			/* quotient */
	movs	r3, #1			/* bit under construction */
	cmp	r1, #0
	beq	4f
1:	lsrs	r4, r1, #31		/* stop before the divisor overflows */
	bne	2f
	cmp	r1, r0
	bhs	2f
	lsls	r1, r1, #1
	lsls	r3, r3, #1
	b	1b
2:	cmp	r0, r1
	blo	3f
	subs	r0, r0, r1
	orrs	r2, r2, r3
3:	lsrs	r1, r1, #1
	lsrs	r3, r3, #1
	bne	2b
4:	movs	r1, r0			/* remainder */
	movs	r0, r2			/* quotient */
	pop	{r4, pc}

	.thumb_func
	.globl	__aeabi_idiv
	.globl	__aeabi_idivmod
	.type	__aeabi_idiv, %function
	.type	__aeabi_idivmod, %function
__aeabi_idiv:
__aeabi_idivmod:
	push	{r4, r5, lr}
	movs	r5, r0			/* dividend sign decides the remainder */
	movs	r4, r0
	eors	r4, r4, r1		/* sign bit set when the signs differ */
	cmp	r0, #0
	bge	1f
	rsbs	r0, r0, #0
1:	cmp	r1, #0
	bge	2f
	rsbs	r1, r1, #0
2:	bl	__aeabi_uidivmod
	cmp	r4, #0
	bge	3f
	rsbs	r0, r0, #0
3:	cmp	r5, #0
	bge	4f
	rsbs	r1, r1, #0
4:	pop	{r4, r5, pc}
