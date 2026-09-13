#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__ledf2)
	push	{r4, lr}
	bl	__rp2040_rom_float_dcmp
	cmp	r0, #2
	bne	Lledf2_return
	movs	r0, #1
Lledf2_return:
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__ledf2)
ROM_FLOAT_ALIAS(__ltdf2, __ledf2)
ROM_FLOAT_ALIAS(__cmpdf2, __ledf2)
ROM_FLOAT_ALIAS(__eqdf2, __ledf2)
ROM_FLOAT_ALIAS(__nedf2, __ledf2)

ROM_FLOAT_FUNCTION(__gedf2)
	push	{r4, lr}
	bl	__rp2040_rom_float_dcmp
	cmp	r0, #2
	bne	Lgedf2_return
	movs	r0, #0
	subs	r0, #1
Lgedf2_return:
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__gedf2)
ROM_FLOAT_ALIAS(__gtdf2, __gedf2)
