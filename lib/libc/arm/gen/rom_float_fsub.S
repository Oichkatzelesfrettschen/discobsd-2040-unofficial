#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(fsub, __aeabi_fsub, 0x4653, 0x04, 1)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__subsf3, __aeabi_fsub)
ROM_FLOAT_FUNCTION(__aeabi_frsub)
	eors	r0, r1
	eors	r1, r0
	eors	r0, r1
	b	__aeabi_fsub
ROM_FLOAT_FUNCTION_END(__aeabi_frsub)
