#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_fcmpgt)
	eors	r0, r1
	eors	r1, r0
	eors	r0, r1
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	adds	r0, #1
	beq	Lfcmpgt_true
	movs	r0, #0
	pop	{r4, pc}
Lfcmpgt_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_fcmpgt)
