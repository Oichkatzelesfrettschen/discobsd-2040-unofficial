#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__aeabi_ul2f)
	cmp	r1, #0
	bne	Lul2f_wide
	ldr	r2, =__aeabi_ui2f
	bx	r2
Lul2f_wide:
	ldr	r2, =ROM_FLOAT_CELL_NAME(ul2f)
	ldr	r3, [r2]
	bx	r3
ROM_FLOAT_FUNCTION_END(__aeabi_ul2f)
ROM_FLOAT_ALIAS(__floatundisf, __aeabi_ul2f)
ROM_FLOAT_TARGET_DESCRIPTOR(ul2f, 0x4653, 0x64, 2)
