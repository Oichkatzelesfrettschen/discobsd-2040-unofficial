#include <stdint.h>
#include <stdio.h>

#ifndef TTY_LOCAL_MODE_FUNCTIONS
#define TTY_LOCAL_MODE_FUNCTIONS "tty_local_mode_functions.c"
#endif
#include TTY_LOCAL_MODE_FUNCTIONS

static unsigned long assertions;

#define CHECK(expression) do {                                                \
	assertions++;                                                          \
	if (!(expression)) {                                                   \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,             \
		    #expression);                                                \
		return 1;                                                       \
	}                                                                      \
} while (0)

int
main(void)
{
	const uint32_t base_flags = UINT32_C(0x5aa5c33c);
	uint32_t local_mode;

	for (local_mode = 0; local_mode <= UINT16_MAX; local_mode++) {
		const uint32_t local_bits = local_mode << 16;

		CHECK(tty_local_mode_bis(base_flags, local_mode) ==
		    (base_flags | local_bits));
		CHECK(tty_local_mode_bic(base_flags, local_mode) ==
		    (base_flags & ~local_bits));
		CHECK(tty_local_mode_set(base_flags, local_mode) ==
		    ((base_flags & UINT32_C(0x0000ffff)) | local_bits));
		CHECK(tty_local_mode_get(local_bits | UINT32_C(0x0000c33c)) ==
		    local_mode);
	}

	CHECK(tty_local_mode_bis(base_flags, UINT32_C(0xffff0001)) ==
	    (base_flags | UINT32_C(0x00010000)));
	CHECK(tty_local_mode_bic(base_flags, UINT32_C(0xffff0001)) ==
	    (base_flags & ~UINT32_C(0x00010000)));
	CHECK(tty_local_mode_set(base_flags, UINT32_C(0xffff0001)) ==
	    UINT32_C(0x0001c33c));

	printf("tty local-mode arithmetic: %lu assertions passed\n", assertions);
	return 0;
}
