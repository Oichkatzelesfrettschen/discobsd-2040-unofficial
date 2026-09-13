@ Assembly written by smlrc's Thumb-1 back end (usr.bin/smlrc/cgthumb.c),
@ the native compiler this assembler exists to serve. Its output differs
@ from the cross compiler's in ways that matter here: it uses the
@ "ldr rN, =expr" literal pool and .ltorg, which GCC never emits; it opens
@ .rodata in the middle of a function, which a pool must survive without
@ being flushed into the instruction stream; and it spells MUL in the
@ three-operand UAL form whose destination repeats in the third position.
@ Each of those was a defect until this input was assembled.
@
@ Generated from usr.bin/smlrc/tests/t01_arith.c; checked in rather than
@ regenerated so the test needs no compiler but the cross one.

	.syntax	unified
	.thumb
	.text
	.globl	main
	.thumb_func
	.type	main, %function
main:
	push	{r0, r1, r2, r3}
	push	{r7, lr}
	mov	r7, sp
	ldr	r3, =.LF0
	mov	r2, sp
	subs	r2, r2, r3
	mov	sp, r2
	push	{r0, r4, r5, r6}
	movs	r0, #47
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #5
	movs	r1, #8
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L3:
	.ascii	"%d %d %d %d\012"
	.space	1

	.text
	sub	sp, #4
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	negs	r0, r0
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	muls	r0, r5, r0
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	subs	r0, r5, r0
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	adds	r0, r5, r0
	push	{r0}
	ldr	r0, =.L3
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	add	sp, #16
	bl	printf
	add	sp, #8

	.section	.rodata,"a",%progbits
.L4:
	.ascii	"%d %d\012"
	.space	1

	.text
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	bl	__aeabi_idivmod
	mov	r0, r1
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	sub	sp, #4
	bl	__aeabi_idiv
	add	sp, #4
	push	{r0}
	ldr	r0, =.L4
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	add	sp, #12
	bl	printf
	movs	r0, #47
	negs	r0, r0
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L5:
	.ascii	"%d %d\012"
	.space	1

	.text
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	bl	__aeabi_idivmod
	mov	r0, r1
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	sub	sp, #4
	bl	__aeabi_idiv
	add	sp, #4
	push	{r0}
	ldr	r0, =.L5
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	add	sp, #12
	bl	printf
	movs	r0, #47
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #5
	negs	r0, r0
	movs	r1, #8
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L6:
	.ascii	"%d %d\012"
	.space	1

	.text
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	bl	__aeabi_idivmod
	mov	r0, r1
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	sub	sp, #4
	bl	__aeabi_idiv
	add	sp, #4
	push	{r0}
	ldr	r0, =.L6
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	add	sp, #12
	bl	printf
	ldr	r0, =-294967296
	movs	r1, #12
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #7
	movs	r1, #16
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L7:
	.ascii	"%u %u\012"
	.space	1

	.text
	movs	r1, #12
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #16
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	bl	__aeabi_uidivmod
	mov	r0, r1
	push	{r0}
	movs	r1, #12
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #16
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	mov	r12, r0
	mov	r0, r5
	mov	r1, r12
	sub	sp, #4
	bl	__aeabi_uidiv
	add	sp, #4
	push	{r0}
	ldr	r0, =.L7
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	add	sp, #12
	bl	printf
	movs	r0, #1
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L8:
	.ascii	"%d %d %d\012"
	.space	1

	.text
	movs	r0, #128
	negs	r0, r0
	push	{r0}
	movs	r0, #128
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	lsls	r0, r0, #10
	push	{r0}
	ldr	r0, =.L8
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	add	sp, #16
	bl	printf
	ldr	r0, =-2147483648
	movs	r1, #12
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L9:
	.ascii	"%u\012"
	.space	1

	.text
	movs	r1, #12
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	lsrs	r0, r0, #28
	push	{r0}
	ldr	r0, =.L9
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	add	sp, #8
	bl	printf
	ldr	r0, =61680
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	ldr	r0, =4080
	movs	r1, #8
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L10:
	.ascii	"%d %d %d %d\012"
	.space	1

	.text
	sub	sp, #4
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	mvns	r0, r0
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	eors	r0, r5
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	orrs	r0, r5
	push	{r0}
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	movs	r1, #8
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	pop	{r5}
	ands	r0, r5
	push	{r0}
	ldr	r0, =.L10
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #12]
	add	sp, #16
	bl	printf
	add	sp, #8
	movs	r0, #5
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #3
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r6, [r1, #0]
	adds	r0, r6, r0
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #1
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r6, [r1, #0]
	subs	r0, r6, r0
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #4
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r6, [r1, #0]
	muls	r0, r6, r0
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	movs	r0, #7
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r6, [r1, #0]
	mov	r12, r0
	mov	r0, r6
	mov	r1, r12
	bl	__aeabi_idiv
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]
	b	.LT1
	.ltorg
.LT1:
	movs	r0, #3
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r6, [r1, #0]
	mov	r12, r0
	mov	r0, r6
	mov	r1, r12
	bl	__aeabi_idivmod
	mov	r0, r1
	movs	r1, #4
	subs	r1, r7, r1
	str	r0, [r1, #0]

	.section	.rodata,"a",%progbits
.L11:
	.ascii	"%d\012"
	.space	1

	.text
	movs	r1, #4
	subs	r1, r7, r1
	ldr	r0, [r1, #0]
	push	{r0}
	ldr	r0, =.L11
	push	{r0}
	ldr	r0, [sp, #0]
	ldr	r1, [sp, #4]
	add	sp, #8
	bl	printf
	movs	r0, #0
	bl	.L1
	movs	r0, #0
.L1:
	pop	{r3, r4, r5, r6}
	mov	sp, r7
	pop	{r7}
	pop	{r3}
	add	sp, #16
	bx	r3
	.equ	.LF0, 16
	.ltorg

 @ Next label number: 12
 @ Compilation succeeded.
