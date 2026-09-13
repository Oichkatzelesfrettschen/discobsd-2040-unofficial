#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_cfcmple)
	push	{r0-r4, lr}
	bl	__rp2040_rom_float_fcmp
	adds	r0, #1
	cmp	r0, #1
	pop	{r0-r4, pc}
ROM_FLOAT_FUNCTION_END(__aeabi_cfcmple)
ROM_FLOAT_ALIAS(__aeabi_cfcmpeq, __aeabi_cfcmple)
