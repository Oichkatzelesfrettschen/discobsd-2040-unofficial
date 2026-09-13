#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_fcmpeq)
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	cmp	r0, #0
	beq	Lfcmpeq_true
	movs	r0, #0
	pop	{r4, pc}
Lfcmpeq_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_fcmpeq)
