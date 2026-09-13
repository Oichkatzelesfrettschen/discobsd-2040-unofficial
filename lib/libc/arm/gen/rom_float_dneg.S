#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_dneg)
	movs	r2, #1
	lsls	r2, r2, #31
	eors	r1, r2
	bx	lr
ROM_FLOAT_FUNCTION_END(__aeabi_dneg)
ROM_FLOAT_ALIAS(__negdf2, __aeabi_dneg)
