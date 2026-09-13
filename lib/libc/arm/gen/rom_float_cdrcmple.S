#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_cdrcmple)
	push	{r0-r7, lr}
	sub	sp, #4
	eors	r0, r2
	eors	r2, r0
	eors	r0, r2
	eors	r1, r3
	eors	r3, r1
	eors	r1, r3
	bl	__rp2040_rom_float_dcmp
	adds	r0, #1
	cmp	r0, #1
	add	sp, #4
	pop	{r0-r7, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_cdrcmple)
