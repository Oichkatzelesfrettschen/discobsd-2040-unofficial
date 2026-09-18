/*
 * Calloc - allocate and clear memory block
 */
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <strings.h>

/*
 * The product num * size is what the caller means and what the block has
 * to hold. size_t is 32 bits on the target, so 0x40000001 elements of four
 * bytes wrap the product to four, and a bare multiply then returns a block
 * a thirtieth of a byte per element. The division form asks whether the
 * product fits before it is formed; a product that does not fit is ENOMEM,
 * the same answer an arena that cannot hold it gives.
 */
void *
calloc(num, size)
	size_t num, size;
{
	register char *p;

	if (size != 0 && num > SIZE_MAX / size) {
		errno = ENOMEM;
		return (NULL);
	}
	size *= num;
	p = malloc(size);
	if (p)
		bzero(p, size);
	return (p);
}

void
cfree(p, num, size)
	char *p;
	unsigned num;
	unsigned size;
{
	(void)num;
	(void)size;
	free(p);
}
