	.syntax unified
	.thumb
	.text
	str	r0, [r1, r2]
	strh	r3, [r4, r5]
	strb	r6, [r7, r0]
	ldrsb	r1, [r2, r3]
	ldr	r4, [r5, r6]
	ldrh	r7, [r0, r1]
	ldrb	r2, [r3, r4]
	ldrsh	r5, [r6, r7]
	str	r0, [r1, #0]
	str	r7, [r6, #124]
	ldr	r0, [r1, #4]
	strb	r2, [r3, #0]
	strb	r4, [r5, #31]
	ldrb	r6, [r7, #17]
	strh	r0, [r1, #0]
	strh	r2, [r3, #62]
	ldrh	r4, [r5, #2]
	ldr	r0, [sp, #0]
	ldr	r7, [sp, #1020]
	str	r1, [sp, #16]
	ldr	r2, [r3]
	str	r4, [r5]
	add	r0, pc, #0
	add	r1, pc, #1020
	add	r2, sp, #0
	add	r7, sp, #1020
	add	sp, #508
	sub	sp, #8
	add	sp, sp, #16
	sub	sp, sp, #4
