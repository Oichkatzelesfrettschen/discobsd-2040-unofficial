#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_DOUBLE_TARGET(dsub, __aeabi_dsub, 0x4453, 0x04, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__subdf3, __aeabi_dsub)
ROM_FLOAT_FUNCTION(__aeabi_drsub)
	eors	r0, r2
	eors	r2, r0
	eors	r0, r2
	eors	r1, r3
	eors	r3, r1
	eors	r1, r3
	b	__aeabi_dsub
ROM_FLOAT_FUNCTION_END(__aeabi_drsub)
