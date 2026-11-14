/*
 * A unit of the 64-bit view of time that reads <signal.h> before any other
 * header. sys/signal.h includes sys/types.h ahead of its own definitions,
 * and sys/types.h reads sys/select.h, which declares pselect() with a
 * sigset_t; the unit compiles only while sys/select.h gets that type without
 * including sys/signal.h a second time. The Makefile compiles it for syntax.
 */
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>

int
signal_first(const sigset_t *mask)
{
	return pselect(0, 0, 0, 0, 0, mask);
}
