/*
 * The one translation unit that sees both sides of the boundary.
 *
 * It includes hostkern.h, which declares what the harness implements, and
 * the kernel's own headers, which declare what a kernel source calls. Any
 * entry point whose signature names size_t has to be defined here, because
 * the kernel's size_t is u_int and the host's is wider; a definition
 * compiled against the host's headers would read its length argument at the
 * wrong width. Including both headers is also what pins panic() and
 * hk_kprintf() to the signatures sys/systm.h declares: a drift in either
 * becomes a conflicting declaration, so the check is the compile.
 *
 * Nothing here calls libc. <string.h> would bring its own size_t, and
 * sys/systm.h's ffs() and the host's disagree on argument type.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>

/*
 * The kernel's bcopy. Kernel callers pass overlapping ranges, so the copy
 * runs backwards when the destination starts inside the source.
 */
void
bcopy(const void *src, void *dst, size_t nbytes)
{
	const char *from = src;
	char *to = dst;

	if (to <= from)
		while (nbytes--)
			*to++ = *from++;
	else {
		from += nbytes;
		to += nbytes;
		while (nbytes--)
			*--to = *--from;
	}
}

/* The kernel's bzero, which sys/systm.h declares beside bcopy. */
void
bzero(void *s, size_t nbytes)
{
	char *p = s;

	while (nbytes--)
		*p++ = '\0';
}
