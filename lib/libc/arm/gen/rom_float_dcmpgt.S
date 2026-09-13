#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_dcmpgt)
	eors	r0, r2
	eors	r2, r0
	eors	r0, r2
	eors	r1, r3
	eors	r3, r1
	eors	r1, r3
	push	{r4, lr}
	bl	__rp2040_rom_float_dcmp
	adds	r0, #1
	beq	Ldcmpgt_true
	movs	r0, #0
	pop	{r4, pc}
Ldcmpgt_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_dcmpgt)
