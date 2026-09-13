	.syntax unified
	.cpu cortex-m0plus
	.thumb
	.text
	.global entry
	.type entry, %function
	.thumb_func
entry:
	movs	r0, #0xd0
	lsls	r0, r0, #24
	movs	r1, #0x30
	lsls	r1, r1, #1
	cmp	r2, r2
	beq	1f
	nop
1:
	ldr	r2, [r0, r1]
	bx	lr
	.size entry, . - entry
