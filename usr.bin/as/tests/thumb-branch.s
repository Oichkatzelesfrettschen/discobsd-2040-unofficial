	.syntax unified
	.thumb
	.text
top:
	b	top
	beq	top
	bne	top
	bcs	top
	bcc	top
	bmi	top
	bpl	top
	bvs	top
	bvc	top
	bhi	top
	bls	top
	bge	top
	blt	top
	bgt	top
	ble	top
	bal	fwd
	bl	top
	bl	fwd
1:
	b	1b
	beq	1f
	nop
1:
	b	1b
fwd:
	adr	r0, .Lpool
	ldr	r2, .Lpool
	ldr	r3, .Lpool+4
	nop
	.align	2
.Lpool:
	.word	0x12345678
	.word	0xdeadbeef
