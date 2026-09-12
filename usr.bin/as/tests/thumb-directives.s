	.syntax unified
	.thumb
	.text
	.global	f
	.type	f, %function
	.thumb_func
f:
	ldr	r0, =0x12345678
	ldr	r1, =extsym
	ldr	r2, =0x12345678
	ldr	r3, =7
	bx	lr
	.ltorg
	.align	2
g:
	ldr	r0, =0xdeadbeef
	bx	lr
	.ltorg
	.data
	.align	2
dw:	.word	0x11223344
	.word	f
	.byte	1, 2, 3
	.align	1
	.hword	0x1234, 0x5678
	.short	9
	.ascii	"abc"
	.asciz	"de"
	.space	4
	.section	.rodata
	.align	2
ro:	.word	42
	.bss
	.align	2
bv:	.space	8
	.comm	cv, 16, 4
	.lcomm	lv, 8
