#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_cdcmple)
	push	{r0-r7, lr}
	sub	sp, #4
	bl	__rp2040_rom_float_dcmp
	adds	r0, #1
	cmp	r0, #1
	add	sp, #4
	pop	{r0-r7, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_cdcmple)
ROM_FLOAT_ALIAS(__aeabi_cdcmpeq, __aeabi_cdcmple)
