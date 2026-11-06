/*
 * Integer division for ARMv6-M, which has no divide instruction. GCC and
 * the tree's Smaller C back end emit calls to the AEABI helpers below; GCC
 * finds them in libgcc, which the board does not carry, so libc provides
 * them. __aeabi_uidivmod returns the quotient in r0 and the remainder in
 * r1, and __aeabi_uidiv is the same entry with the remainder ignored; the
 * signed pair works on magnitudes and restores the signs afterwards: the
 * quotient is negative when the operand signs differ, the remainder takes
 * the dividend's sign (C99 6.5.5). Division by zero returns a zero
 * quotient and the dividend as remainder. The helpers clobber r0-r3, ip
 * and the flags and use no stack.
 *
 * The division runs on the SIO hardware divider of the executing core
 * (RP2040 datasheet 2.3.1.5). Writing DIV_UDIVIDEND (SIO offset 0x60) and
 * then DIV_UDIVISOR (0x64) starts an unsigned division; the results appear
 * in DIV_REMAINDER (0x74) and DIV_QUOTIENT (0x70) on the ninth cycle after
 * the divisor write, so eight cycles of straight-line code separate the
 * write from the first read. The unsigned alias serves both pairs. The
 * signed pair divides magnitudes, which makes INT_MIN divided by -1 the
 * 2^31 quotient and zero remainder the software helpers returned without
 * depending on the hardware's overflow behavior, and both pairs test the
 * divisor in software because the hardware answers a zero divisor with an
 * all-ones quotient. The remainder is read before the quotient: reading
 * DIV_QUOTIENT clears DIV_CSR.DIRTY, the flag a divider save and restore
 * consults to decide whether a result is still pending.
 *
 * The sequence from the dividend write to the quotient read is one atomic
 * unit because nothing saves the divider, a per-core state. No kernel path
 * switches processes, delivers a signal or runs divider code between two
 * user instructions: process switches and signals happen only on the
 * return from a system call or fault (userret), a SysTick or device
 * interrupt returns to the interrupted instruction, and the kernel reaches
 * the divider nowhere (tools/verify_rp2040_divider_ownership.py rejects an
 * access in every linked kernel and holds this file's object to a
 * straight-line window with no system call, call or breakpoint). The user
 * MPU map grants the divider registers (sys/arch/rp2040/doc/MPU.md).
 *
 * DIVIDER_MODEL replaces the base address and the eight-cycle wait with a
 * RAM register block and a call into the test harness that computes the
 * results from it (tests/rp2040/divider_helpers); the library never
 * defines it.
 */
#ifdef DIVIDER_MODEL
/* Thumb-1 cannot pop lr, so the model's call keeps it in r0 across the call. */
#define DIVIDER_MODEL_WAIT \
	push	{r0, r1}; \
	mov	r0, lr; \
	push	{r0}; \
	bl	divider_model_step; \
	pop	{r0}; \
	mov	lr, r0; \
	pop	{r0, r1}
#endif

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
	cmp	r1, #0
	beq	2f
#ifdef DIVIDER_MODEL
	ldr	r3, =divider_model_sio
#else
	movs	r3, #0xd0
	lsls	r3, r3, #24		/* SIO base 0xd0000000 */
#endif
	str	r0, [r3, #0x60]		/* DIV_UDIVIDEND */
	str	r1, [r3, #0x64]		/* DIV_UDIVISOR starts the division */
#ifdef DIVIDER_MODEL
	DIVIDER_MODEL_WAIT
#else
	b	1f			/* four taken branches take eight cycles */
1:	b	1f
1:	b	1f
1:	b	1f
1:
#endif
	ldr	r1, [r3, #0x74]		/* DIV_REMAINDER */
	ldr	r0, [r3, #0x70]		/* DIV_QUOTIENT, read last */
	bx	lr
2:	movs	r1, r0			/* zero divisor: remainder is the dividend */
	movs	r0, #0
	bx	lr
#ifdef DIVIDER_MODEL
	.pool
#endif

	.thumb_func
	.globl	__aeabi_idiv
	.globl	__aeabi_idivmod
	.type	__aeabi_idiv, %function
	.type	__aeabi_idivmod, %function
__aeabi_idiv:
__aeabi_idivmod:
	asrs	r3, r0, #31		/* dividend sign mask, -1 when negative */
	eors	r0, r3
	subs	r0, r0, r3		/* |dividend|; INT_MIN stays 2^31 */
	mov	ip, r3			/* the remainder takes the dividend's sign */
	asrs	r2, r1, #31		/* divisor sign mask */
	eors	r1, r2
	subs	r1, r1, r2		/* |divisor|, zero only for a zero divisor */
	beq	2f
	eors	r2, r3			/* quotient sign mask: the signs differ */
#ifdef DIVIDER_MODEL
	ldr	r3, =divider_model_sio
#else
	movs	r3, #0xd0
	lsls	r3, r3, #24		/* SIO base 0xd0000000 */
#endif
	str	r0, [r3, #0x60]		/* DIV_UDIVIDEND */
	str	r1, [r3, #0x64]		/* DIV_UDIVISOR starts the division */
	mov	r0, ip			/* this move and four taken branches */
#ifdef DIVIDER_MODEL
	DIVIDER_MODEL_WAIT
#else
	b	1f			/* take nine cycles */
1:	b	1f
1:	b	1f
1:	b	1f
1:
#endif
	ldr	r1, [r3, #0x74]		/* DIV_REMAINDER */
	ldr	r3, [r3, #0x70]		/* DIV_QUOTIENT, read last */
	eors	r1, r0
	subs	r1, r1, r0		/* remainder takes the dividend's sign */
	eors	r3, r2
	subs	r0, r3, r2		/* quotient negative when the signs differ */
	bx	lr
2:	mov	r1, ip			/* zero divisor: rebuild the dividend */
	eors	r0, r1
	subs	r1, r0, r1
	movs	r0, #0
	bx	lr
#ifdef DIVIDER_MODEL
	.pool
#endif
