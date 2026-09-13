#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_fcmpun)
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	cmp	r0, #2
	beq	Lfcmpun_true
	movs	r0, #0
	pop	{r4, pc}
Lfcmpun_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_fcmpun)
ROM_FLOAT_ALIAS(__unordsf2, __aeabi_fcmpun)
