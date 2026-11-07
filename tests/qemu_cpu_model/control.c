/*
 * Negative controls for the qemu-arm CPU model. The program executes the one
 * operation its build selects and exits 0 when the CPU accepted it:
 *
 *   -DCONTROL_BASELINE   a Thumb-1 register move; every model accepts it
 *   -DCONTROL_THUMB2     a Thumb-2 movw; ARMv6-M has no such encoding
 *   -DCONTROL_UNALIGNED  a word load from an odd address; ARMv6-M faults
 *
 * It is freestanding with Linux EABI exit_group, so the CPU model is the only
 * thing under test.
 */

static unsigned char word[8] __attribute__((aligned(4), unused)) = {1, 2, 3, 4, 5, 6, 7, 8};

static void __attribute__((noreturn))
leave(long status)
{
	register long r7 __asm__("r7") = 248;
	register long r0 __asm__("r0") = status;

	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
	for (;;)
		;
}

void control_main(void) __attribute__((used, noreturn));

void
control_main(void)
{
	unsigned value = 0;

#if defined(CONTROL_BASELINE)
	__asm__ volatile("movs %0, #1" : "=l"(value));
#elif defined(CONTROL_THUMB2)
	__asm__ volatile(".arch armv7-m\n\t.syntax unified\n\tmovw %0, #0x1234"
	    : "=r"(value));
#elif defined(CONTROL_UNALIGNED)
	__asm__ volatile("ldr %0, [%1]" : "=l"(value) : "l"(word + 1));
#else
#error select a control with -DCONTROL_BASELINE, -DCONTROL_THUMB2 or -DCONTROL_UNALIGNED
#endif
	leave(value == 0);
}

__asm__(
"	.thumb\n"
"	.globl	_start\n"
"	.thumb_func\n"
"_start:\n"
"	bl	control_main\n");
