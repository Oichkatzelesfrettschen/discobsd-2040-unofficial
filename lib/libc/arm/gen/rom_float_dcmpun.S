#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_dcmpun)
	push	{r4, lr}
	bl	__rp2040_rom_float_dcmp
	cmp	r0, #2
	beq	Ldcmpun_true
	movs	r0, #0
	pop	{r4, pc}
Ldcmpun_true:
	movs	r0, #1
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_dcmpun)
ROM_FLOAT_ALIAS(__unorddf2, __aeabi_dcmpun)
