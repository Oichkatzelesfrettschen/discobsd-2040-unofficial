#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_fneg)
	movs	r1, #1
	lsls	r1, r1, #31
	eors	r0, r1
	bx	lr
ROM_FLOAT_FUNCTION_END(__aeabi_fneg)
ROM_FLOAT_ALIAS(__negsf2, __aeabi_fneg)
