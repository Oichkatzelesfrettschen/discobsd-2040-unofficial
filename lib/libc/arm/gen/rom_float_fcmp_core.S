#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__rp2040_rom_float_fcmp)
	movs	r2, #0xff
	lsls	r2, r2, #24
	lsls	r3, r0, #1
	cmp	r3, r2
	bhi	Lfcmp_unordered
	lsls	r3, r1, #1
	cmp	r3, r2
	bhi	Lfcmp_unordered

	cmp	r0, r1
	beq	Lfcmp_equal
	lsls	r2, r0, #1
	lsls	r3, r1, #1
	orrs	r2, r3
	beq	Lfcmp_equal

	mov	r3, r0
	eors	r3, r1
	bmi	Lfcmp_opposite_signs
	cmp	r0, #0
	bmi	Lfcmp_negative
	cmp	r0, r1
	bcc	Lfcmp_less
	b	Lfcmp_greater
Lfcmp_negative:
	cmp	r1, r0
	bcc	Lfcmp_less
	b	Lfcmp_greater
Lfcmp_opposite_signs:
	cmp	r0, #0
	bmi	Lfcmp_less
	b	Lfcmp_greater
Lfcmp_equal:
	movs	r0, #0
	bx	lr
Lfcmp_less:
	movs	r0, #0
	subs	r0, #1
	bx	lr
Lfcmp_greater:
	movs	r0, #1
	bx	lr
Lfcmp_unordered:
	movs	r0, #2
	bx	lr
ROM_FLOAT_FUNCTION_END(__rp2040_rom_float_fcmp)
