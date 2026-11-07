/*
 * The shift-and-subtract division helpers the library used before it moved
 * to the SIO divider, kept under ref_ names as the software oracle for
 * tests/rp2040/divider_helpers: the new helpers must return the same
 * quotient and remainder for every operand pair, zero divisors and
 * INT_MIN / -1 included. This file is test input and never reaches a
 * library or an image.
 */
	.syntax	unified
	.thumb
	.text

	.thumb_func
	.globl	ref_aeabi_uidiv
	.globl	ref_aeabi_uidivmod
	.type	ref_aeabi_uidiv, %function
	.type	ref_aeabi_uidivmod, %function
ref_aeabi_uidiv:
ref_aeabi_uidivmod:
	push	{r4, lr}
	movs	r2, #0			/* quotient */
	movs	r3, #1			/* bit under construction */
	cmp	r1, #0
	beq	4f
1:	lsrs	r4, r1, #31		/* stop before the divisor overflows */
	bne	2f
	cmp	r1, r0
	bcs	2f			/* r1 >= r0: bhs, the tree's as only knows bcs */
	lsls	r1, r1, #1
	lsls	r3, r3, #1
	b	1b
2:	cmp	r0, r1
	bcc	3f			/* r0 < r1: blo, the tree's as only knows bcc */
	subs	r0, r0, r1
	orrs	r2, r2, r3
3:	lsrs	r1, r1, #1
	lsrs	r3, r3, #1
	bne	2b
4:	movs	r1, r0			/* remainder */
	movs	r0, r2			/* quotient */
	pop	{r4, pc}

	.thumb_func
	.globl	ref_aeabi_idiv
	.globl	ref_aeabi_idivmod
	.type	ref_aeabi_idiv, %function
	.type	ref_aeabi_idivmod, %function
ref_aeabi_idiv:
ref_aeabi_idivmod:
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
2:	bl	ref_aeabi_uidivmod
	cmp	r4, #0
	bge	3f
	rsbs	r0, r0, #0
3:	cmp	r5, #0
	bge	4f
	rsbs	r1, r1, #0
4:	pop	{r4, r5, pc}
