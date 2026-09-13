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
	ldr	r1, [r0, #0x60]
	movs	r1, #0x30
	lsls	r1, r1, #1
	ldr	r2, [r0, r1]
	ldr	r3, divider_csr_address
	ldr	r3, [r3]
	bx	lr
	.align 2
divider_csr_address:
	.word	0xd0000078
	.size entry, . - entry
