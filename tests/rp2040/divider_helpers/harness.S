/*
 * Runtime for divider_test.c: the process entry, the Linux EABI system
 * calls, the register-preservation wrapper and the divider model hook.
 *
 * lib/libc/arm/gen/aeabi_div.S is assembled with DIVIDER_MODEL, which
 * points its base register at divider_model_sio and calls
 * divider_model_step where the hardware build waits eight cycles. The
 * step computes DIV_QUOTIENT and DIV_REMAINDER from DIV_UDIVIDEND and
 * DIV_UDIVISOR, the contract of RP2040 datasheet 2.3.1.5 for an unsigned
 * division, so the helper's operand order, register offsets, result order
 * and sign handling run as shipped and only the divider's own arithmetic
 * and timing are modeled.
 */
	.syntax	unified
	.thumb
	.text

	.globl	_start
	.thumb_func
_start:
	bl	main
	movs	r7, #248		/* exit_group */
	svc	#0
	b	.

/*
 * void harness_write(const char *text, unsigned length)
 */
	.globl	harness_write
	.thumb_func
harness_write:
	push	{r7, lr}
	movs	r2, r1
	movs	r1, r0
	movs	r0, #2			/* standard error */
	movs	r7, #4			/* write */
	svc	#0
	pop	{r7, pc}

/*
 * unsigned harness_call(void *helper, unsigned a, unsigned b,
 *     unsigned out[2])
 *
 * Calls helper with a in r0 and b in r1 after loading r4-r11 with distinct
 * values, stores the returned r0 and r1 through out and returns a mask
 * with bit n set when r(4+n) changed. The AEABI division helpers may
 * clobber r0-r3, ip and the flags and nothing else.
 */
	.macro	check_low reg, bit
	ldr	r2, =(0x44444444 + (\reg - 4) * 0x01010101)
	cmp	r\reg, r2
	beq	1f
	movs	r2, #(1 << \bit)
	orrs	r0, r2
1:
	.endm

	.macro	check_high reg, bit
	ldr	r2, =(0x44444444 + (\reg - 4) * 0x01010101)
	mov	r3, r\reg
	cmp	r3, r2
	beq	1f
	movs	r2, #(1 << \bit)
	orrs	r0, r2
1:
	.endm

	.macro	load_low reg
	ldr	r\reg, =(0x44444444 + (\reg - 4) * 0x01010101)
	.endm

	.macro	load_high reg
	ldr	r2, =(0x44444444 + (\reg - 4) * 0x01010101)
	mov	r\reg, r2
	.endm

	.globl	harness_call
	.thumb_func
harness_call:
	push	{r4, r5, r6, r7, lr}
	mov	r4, r8
	mov	r5, r9
	mov	r6, r10
	mov	r7, r11
	push	{r4, r5, r6, r7}
	push	{r3}			/* out */
	mov	ip, r0			/* helper */
	movs	r0, r1
	movs	r1, r2
	load_low 4
	load_low 5
	load_low 6
	load_low 7
	load_high 8
	load_high 9
	load_high 10
	load_high 11
	mov	r2, ip
	blx	r2
	ldr	r3, [sp, #0]
	str	r0, [r3, #0]
	str	r1, [r3, #4]
	movs	r0, #0			/* mask of changed registers */
	check_low 4, 0
	check_low 5, 1
	check_low 6, 2
	check_low 7, 3
	check_high 8, 4
	check_high 9, 5
	check_high 10, 6
	check_high 11, 7
	pop	{r3}
	pop	{r4, r5, r6, r7}
	mov	r8, r4
	mov	r9, r5
	mov	r10, r6
	mov	r11, r7
	pop	{r4, r5, r6, r7, pc}
	.pool

/*
 * The model's wait. Preserves r0-r3, ip and the stack; the helper has
 * saved lr.
 */
	.globl	divider_model_step
	.thumb_func
divider_model_step:
	push	{r0, r1, r2, r3, lr}
	mov	r0, ip
	push	{r0}
	bl	divider_model_compute
	pop	{r0}
	mov	ip, r0
	pop	{r0, r1, r2, r3, pc}
