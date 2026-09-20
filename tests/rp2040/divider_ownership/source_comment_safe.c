/*
 * Positive control for the source scan: every divider register named here
 * is named in a comment or a rejected-lookalike position, and the verifier
 * must report no source finding. The kernel needs this because
 * sys/arch/rp2040/rp2040/mpu.c states which registers the boot ROM's
 * fdiv_n writes -- DIV_UDIVIDEND, DIV_UDIVISOR and DIV_QUOTIENT -- while
 * programming an MPU region rather than dividing.
 */

/* A block comment on one line naming DIV_CSR and 0xd0000078. */

// A line comment naming DIV_REMAINDER and SIO_BASE + 0x74.

unsigned int
sio_base(void)
{
	/* 0xd0000060 in a comment beside the code it explains. */
	return 0xd0000000U;	/* DIV_SDIVIDEND lives 0x68 above this. */
}

/*
 * A block comment that spans lines and names DIV_QUOTIENT_OFFSET on one of
 * them, so the block state has to carry across the newline.
 */
unsigned int
divider_free(void)
{
	return 0xd0000004U;	/* GPIO_IN, not a divider register. */
}
