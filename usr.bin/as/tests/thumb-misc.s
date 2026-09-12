	.syntax unified
	.thumb
	.text
	sxth	r0, r1
	sxtb	r2, r3
	uxth	r4, r5
	uxtb	r6, r7
	rev	r0, r1
	rev16	r2, r3
	revsh	r4, r5
	push	{r0}
	push	{r0, r1, r2, r3, r4, r5, r6, r7, lr}
	push	{r4, lr}
	push	{lr}
	pop	{r0}
	pop	{r4, r5, pc}
	pop	{pc}
	stmia	r0!, {r1, r2}
	ldmia	r3!, {r4, r5, r6}
	stm	r7!, {r0}
	nop
	yield
	wfe
	wfi
	sev
	svc	#0
	svc	#255
	bkpt	#12
	udf	#7
	cpsie	i
	cpsid	i
	mrs	r0, primask
	msr	primask, r1
	mrs	r2, apsr
	mrs	r3, xpsr
	mrs	r4, ipsr
	mrs	r5, msp
	mrs	r6, psp
	msr	control, r7
	msr	msp, r0
	dmb
	dsb
	isb
	dmb	sy
	dsb	sy
	isb	sy
