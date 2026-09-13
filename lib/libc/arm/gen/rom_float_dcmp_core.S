#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__rp2040_rom_float_dcmp)
	push	{r4, r5, lr}
	ldr	r4, =0xffe00000
	lsls	r5, r1, #1
	cmp	r5, r4
	bhi	Ldcmp_unordered
	bne	Ldcmp_check_second
	cmp	r0, #0
	bne	Ldcmp_unordered
Ldcmp_check_second:
	lsls	r5, r3, #1
	cmp	r5, r4
	bhi	Ldcmp_unordered
	bne	Ldcmp_equality
	cmp	r2, #0
	bne	Ldcmp_unordered

Ldcmp_equality:
	cmp	r1, r3
	bne	Ldcmp_zero
	cmp	r0, r2
	beq	Ldcmp_equal
Ldcmp_zero:
	mov	r4, r0
	orrs	r4, r2
	bne	Ldcmp_order
	lsls	r4, r1, #1
	lsls	r5, r3, #1
	orrs	r4, r5
	beq	Ldcmp_equal

Ldcmp_order:
	mov	r4, r1
	eors	r4, r3
	bmi	Ldcmp_opposite_signs
	cmp	r1, #0
	bmi	Ldcmp_negative
	cmp	r1, r3
	bcc	Ldcmp_less
	bhi	Ldcmp_greater
	cmp	r0, r2
	bcc	Ldcmp_less
	b	Ldcmp_greater
Ldcmp_negative:
	cmp	r3, r1
	bcc	Ldcmp_less
	bhi	Ldcmp_greater
	cmp	r2, r0
	bcc	Ldcmp_less
	b	Ldcmp_greater
Ldcmp_opposite_signs:
	cmp	r1, #0
	bmi	Ldcmp_less
	b	Ldcmp_greater
Ldcmp_equal:
	movs	r0, #0
	pop	{r4, r5, pc}
Ldcmp_less:
	movs	r0, #0
	subs	r0, #1
	pop	{r4, r5, pc}
Ldcmp_greater:
	movs	r0, #1
	pop	{r4, r5, pc}
Ldcmp_unordered:
	movs	r0, #2
	pop	{r4, r5, pc}
	.pool
ROM_FLOAT_FUNCTION_END(__rp2040_rom_float_dcmp)
