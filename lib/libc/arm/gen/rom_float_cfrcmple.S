#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_cfrcmple)
	push	{r0-r4, lr}
	eors	r0, r1
	eors	r1, r0
	eors	r0, r1
	bl	__rp2040_rom_float_fcmp
	adds	r0, #1
	cmp	r0, #1
	pop	{r0-r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_cfrcmple)
