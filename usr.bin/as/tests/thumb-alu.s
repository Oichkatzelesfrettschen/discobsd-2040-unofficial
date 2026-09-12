	.syntax unified
	.thumb
	.text
	ands	r0, r1
	eors	r2, r3
	lsls	r4, r5
	lsrs	r6, r7
	asrs	r0, r1
	adcs	r2, r3
	sbcs	r4, r5
	rors	r6, r7
	tst	r0, r1
	rsbs	r2, r3, #0
	negs	r4, r5
	cmp	r6, r7
	cmn	r0, r1
	orrs	r2, r3
	muls	r4, r5
	bics	r6, r7
	mvns	r0, r1
	add	r8, r9
	add	sp, r10
	cmp	r11, r12
	mov	r13, r14
	mov	r0, r8
	bx	lr
	bx	r3
	blx	r2
