#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(d2ulz, __aeabi_d2ulz, 0x4453, 0x74, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__fixunsdfdi, __aeabi_d2ulz)

ROM_FLOAT_FUNCTION(__aeabi_d2lz)
	asrs	r2, r1, #31
	push	{r2, lr}
	lsls	r1, r1, #1
	lsrs	r1, r1, #1
	bl	__aeabi_d2ulz
	pop	{r2, r3}
	mov	lr, r3
	cmp	r1, #0
	bmi	Ld2lz_saturate
	cmp	r2, #0
	beq	Ld2lz_return
	movs	r3, #0
	negs	r0, r0
	sbcs	r3, r1
	mov	r1, r3
Ld2lz_return:
	bx	lr
Ld2lz_saturate:
	movs	r0, #0
	movs	r1, #0x80
	lsls	r1, r1, #24
	cmp	r2, #0
	bne	Ld2lz_return
	subs	r0, #1
	subs	r1, #1
	bx	lr
ROM_FLOAT_FUNCTION_END(__aeabi_d2lz)
ROM_FLOAT_ALIAS(__fixdfdi, __aeabi_d2lz)
