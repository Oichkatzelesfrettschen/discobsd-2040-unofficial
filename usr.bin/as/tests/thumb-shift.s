	.syntax unified
	.thumb
	.text
	lsls	r0, r1, #0
	lsls	r7, r6, #31
	lsrs	r1, r2, #1
	lsrs	r3, r4, #32
	asrs	r5, r6, #7
	asrs	r0, r0, #32
	adds	r0, r1, r2
	adds	r7, r7, r7
	subs	r3, r4, r5
	adds	r0, r1, #0
	adds	r2, r3, #7
	subs	r4, r5, #1
	movs	r0, #0
	movs	r7, #255
	cmp	r1, #128
	adds	r2, #200
	subs	r3, #17
