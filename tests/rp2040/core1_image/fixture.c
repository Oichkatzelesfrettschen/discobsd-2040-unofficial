/*
 * Fixture for tools/verify_rp2040_core1_image.py: the three functions core 1
 * enters by address, with one deliberate defect selected by a macro. With no
 * macro defined everything core 1 reaches is in the SRAM section and the
 * verifier has to pass; each macro breaks one rule and has to be reported.
 */

#define	RAM	__attribute__((noinline, section(".ramfunc")))

typedef void handler_t(unsigned int);

volatile unsigned int sink;
const unsigned int flash_word = 0x12345678;

__attribute__((noinline)) unsigned int
flash_function(unsigned int v)
{
	return v + sink;
}

RAM void
cring_service(handler_t *fn, unsigned int v)
{
	fn(v);
}

RAM void
core1_op(unsigned int v)
{
#ifdef BAD_LITERAL
	v += flash_word;
#endif
	sink = v;
}

RAM void
core1_fault(void)
{
#ifdef BAD_TRAP
	__asm__ volatile ("svc 0");
#endif
	for (;;)
		continue;
}

RAM void
core1_main(void)
{
#ifdef BAD_INDIRECT
	handler_t *volatile fn = core1_op;

	fn(1);
#endif
	for (;;) {
		cring_service(core1_op, sink);
#ifdef BAD_CALL
		sink = flash_function(sink);
#endif
	}
}

void
entry(void)
{
	core1_main();
}
