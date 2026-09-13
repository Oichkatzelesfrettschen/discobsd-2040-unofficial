#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_fcmple)
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	adds	r0, #1
	cmp	r0, #1
	bls	Lfcmple_true
	movs	r0, #0
	pop	{r4, pc}
Lfcmple_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_fcmple)
