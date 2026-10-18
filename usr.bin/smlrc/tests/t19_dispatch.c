/* The native subset accepts the private kernel dispatch declarations. */
#define KERNEL
#include <sys/syscall_dispatch.h>
#include <sys/syscall_dispatch.h>
#include <sys/glob.h>
#include <sys/glob.h>
#undef KERNEL

static int observed;

static void
record_dispatch(void)
{
	observed = 37;
}

int
main(void)
{
	syscall_handler_t handler;
	unsigned char argument_count;

	handler = record_dispatch;
	argument_count = 6;
	handler();
	return observed != 37 || argument_count != 6 ||
	    sizeof(handler) != 4 || sizeof(argument_count) != 1;
}
