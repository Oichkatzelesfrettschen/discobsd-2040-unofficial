/*
 * Resolve one RP2040 Boot ROM target and publish it in its demand-linked
 * cache cell. The resolver accepts the public V1 single-precision surface
 * and the V2/V3 single- and double-precision surfaces.
 */
#include "rom_float.inc"
#include <machine/machparam.h>

ROM_FLOAT_SETUP
ROM_FLOAT_TEXT
ROM_FLOAT_FUNCTION(__rp2040_rom_float_resolve)
	push	{r0-r4, lr}
	/* The cold trampoline keeps the caller's return address in ip. Its
	 * long-range bl puts the odd descriptor address in lr. Preserve the
	 * caller address in the saved-lr slot before the ROM lookup clobbers ip. */
	mov	r4, lr
	subs	r4, #1
	mov	r3, ip
	str	r3, [sp, #20]

	/* Validate the public magic, compatibility byte and version floor. */
	movs	r0, #0x10
	ldrb	r1, [r0, #0]
	cmp	r1, #77
	bne	Lrom_float_invalid
	ldrb	r1, [r0, #1]
	cmp	r1, #117
	bne	Lrom_float_invalid
	ldrb	r1, [r0, #2]
	cmp	r1, #1
	bne	Lrom_float_invalid
	ldrb	r1, [r4, #7]
	lsls	r1, r1, #25
	lsrs	r1, r1, #25
	ldrb	r2, [r0, #3]
	cmp	r2, r1
	bcc	Lrom_float_invalid

	/* A corrupted descriptor cannot publish outside the user RAM window. */
	ldr	r1, [r4, #0]
	movs	r2, #3
	tst	r1, r2
	bne	Lrom_float_invalid
	ldr	r2, =USER_DATA_START
	cmp	r1, r2
	bcc	Lrom_float_invalid
	ldr	r2, =USER_DATA_END
	cmp	r1, r2
	bcs	Lrom_float_invalid

	/* Validate the public lookup helper before executing it. */
	movs	r0, #0x18
	ldrh	r3, [r0]
	cmp	r3, #0
	beq	Lrom_float_invalid
	movs	r2, #1
	tst	r3, r2
	beq	Lrom_float_invalid
	lsrs	r2, r3, #14
	bne	Lrom_float_invalid

	/* Bit 7 selects the function table; floating tables use the data table. */
	ldrb	r2, [r4, #7]
	lsls	r2, r2, #24
	bmi	Lrom_float_function_table
	movs	r0, #0x16
	b	Lrom_float_table_pointer
Lrom_float_function_table:
	movs	r0, #0x14
Lrom_float_table_pointer:
	ldrh	r0, [r0]
	cmp	r0, #0
	beq	Lrom_float_invalid
	movs	r2, #1
	tst	r0, r2
	bne	Lrom_float_invalid
	lsrs	r2, r0, #14
	bne	Lrom_float_invalid
	ldrh	r1, [r4, #4]
	blx	r3

	ldrb	r1, [r4, #7]
	lsls	r1, r1, #24
	bmi	Lrom_float_function_target

	/* Validate a floating table and the requested entry before loading it. */
	cmp	r0, #0
	beq	Lrom_float_invalid
	movs	r2, #3
	tst	r0, r2
	bne	Lrom_float_invalid
	lsrs	r2, r0, #14
	bne	Lrom_float_invalid
	/* Table length by ROM version. Boot ROM V1 exposes 0x54 bytes of the
	 * single table. V2 and V3 expose 0x80 bytes of both the single and
	 * the double table (Pico SDK sf_table.h SF_TABLE_V2_SIZE and
	 * double_init_rom_rp2040.c). Only the single table carries a length
	 * byte before it; on a V3 ROM the byte before the double table
	 * reads 0, so a length read there rejects every double entry. */
	movs	r1, #0x13
	ldrb	r1, [r1]
	cmp	r1, #2
	bcc	Lrom_float_v1_size
	movs	r1, #0x80
	b	Lrom_float_check_size
Lrom_float_v1_size:
	movs	r1, #0x54
Lrom_float_check_size:
	ldrb	r2, [r4, #6]
	adds	r2, #4
	cmp	r1, r2
	bcc	Lrom_float_invalid
	subs	r2, #4
	ldr	r3, [r0, r2]
	b	Lrom_float_validate_target

Lrom_float_function_target:
	mov	r3, r0
Lrom_float_validate_target:
	cmp	r3, #0
	beq	Lrom_float_invalid
	movs	r2, #1
	tst	r3, r2
	beq	Lrom_float_invalid
	lsrs	r2, r3, #14
	bne	Lrom_float_invalid

	ldr	r1, [r4, #0]
	str	r3, [r1]
	mov	ip, r3
	ldr	r4, [sp, #20]
	mov	lr, r4
	pop	{r0-r4}
	add	sp, #4
	bx	ip

Lrom_float_invalid:
	movs	r0, #70
	svc	#1
Lrom_float_invalid_return:
	b	Lrom_float_invalid_return
	.pool
ROM_FLOAT_FUNCTION_END(__rp2040_rom_float_resolve)
