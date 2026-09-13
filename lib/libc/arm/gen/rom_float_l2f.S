#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_l2f)
	asrs	r2, r0, #31
	cmp	r1, r2
	bne	Ll2f_wide
	ldr	r2, =__aeabi_i2f
	bx	r2
Ll2f_wide:
	ldr	r2, =ROM_FLOAT_CELL_NAME(l2f)
	ldr	r3, [r2]
	bx	r3
ROM_FLOAT_FUNCTION_END(__aeabi_l2f)
ROM_FLOAT_ALIAS(__floatdisf, __aeabi_l2f)
ROM_FLOAT_TARGET_DESCRIPTOR(l2f, 0x4653, 0x5c, 2)
