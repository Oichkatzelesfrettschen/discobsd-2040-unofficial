/*
 * Integer division for ARMv7-M (Cortex-M4, the STM32 machines), which has the
 * SDIV and UDIV instructions. GCC and the tree's Smaller C back end emit calls
 * to the AEABI helpers below (usr.bin/smlrc/cgthumb.c, ThumbDivMod);
 * __aeabi_uidivmod and __aeabi_idivmod return the quotient in r0 and the
 * remainder in r1, and __aeabi_uidiv and __aeabi_idiv are the same entries
 * with the remainder ignored. The remainder is dividend - quotient * divisor
 * (MLS), so it takes the dividend's sign for the signed pair (C99 6.5.5), and
 * INT_MIN divided by -1 yields the 2^31 quotient (SDIV's defined overflow
 * result) with a zero remainder. A zero divisor yields a zero quotient and the
 * dividend as remainder because CCR.DIV_0_TRP resets clear and no STM32 start
 * path sets it (ARMv7-M ARM B3.2.8). The helpers clobber r0-r2, leave the
 * flags alone and use no stack.
 *
 * lib/libc/arm/gen/aeabi_div.S carries the RP2040 version, which runs on the
 * SIO divider that no other machine has; the Makefile selects one source per
 * MACHINE (DIV_SRC), and tests/rp2040/divider_helpers runs this source as a
 * Cortex-M4 program.
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
	udiv	r2, r0, r1
	mls	r1, r2, r1, r0		/* remainder: dividend - quotient * divisor */
	mov	r0, r2
	bx	lr

	.thumb_func
	.globl	__aeabi_idiv
	.globl	__aeabi_idivmod
	.type	__aeabi_idiv, %function
	.type	__aeabi_idivmod, %function
__aeabi_idiv:
__aeabi_idivmod:
	sdiv	r2, r0, r1
	mls	r1, r2, r1, r0		/* remainder takes the dividend's sign */
	mov	r0, r2
	bx	lr
