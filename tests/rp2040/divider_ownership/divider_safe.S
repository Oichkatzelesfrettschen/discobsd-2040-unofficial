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
	ldr	r1, [r0, #0x04]
	str	r1, [r0, #0x14]
	bx	lr
	.size entry, . - entry
