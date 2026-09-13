#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__lesf2)
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	cmp	r0, #2
	bne	Llesf2_return
	movs	r0, #1
Llesf2_return:
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__lesf2)
ROM_FLOAT_ALIAS(__ltsf2, __lesf2)
ROM_FLOAT_ALIAS(__cmpsf2, __lesf2)
ROM_FLOAT_ALIAS(__eqsf2, __lesf2)
ROM_FLOAT_ALIAS(__nesf2, __lesf2)

ROM_FLOAT_FUNCTION(__gesf2)
	push	{r4, lr}
	bl	__rp2040_rom_float_fcmp
	cmp	r0, #2
	bne	Lgesf2_return
	movs	r0, #0
	subs	r0, #1
Lgesf2_return:
	pop	{r4, pc}
ROM_FLOAT_FUNCTION_END(__gesf2)
ROM_FLOAT_ALIAS(__gtsf2, __gesf2)
