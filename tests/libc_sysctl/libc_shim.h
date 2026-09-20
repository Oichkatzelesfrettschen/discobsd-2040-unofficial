/*
 * Reporting for a gate that carries the tree's own userland headers.
 *
 * include/sys/types.h reads "typedef u_int size_t" and include/stdio.h spells
 * stderr as &_iob[2], so a translation unit that included both the tree's
 * headers and the host's would take a four-byte size_t from one and an eight
 * byte one from the other, and would call the host's fprintf with a FILE the
 * host does not own. The gate therefore reaches the tree's headers alone, and
 * this header names the only things it borrows from the host. libc_shim.c is
 * the one translation unit on the other side, and it carries no tree header.
 *
 * The same split is why the tier builds at ILP32: at that width the tree's
 * size_t and the host's unsigned are the same four bytes, so a length that
 * crosses the boundary means the same thing to both sides.
 */
#ifndef LIBC_SHIM_H
#define LIBC_SHIM_H

void	lsys_fail(const char *file, int line, const char *what);
void	lsys_note(const char *msg);
int	lsys_verdict(const char *suite);

/*
 * An allocation of exactly the size asked for, so that a sanitizer's redzone
 * sits immediately past it: a structure placed here turns a walk that runs
 * off its end into a fault the sanitizer names, which a structure inside a
 * larger object cannot do.
 */
void   *lsys_alloc(unsigned nbytes);
void	lsys_free(void *p);

extern unsigned lsys_checks;
extern unsigned lsys_failures;

#define CHECK(cond)							\
	do {								\
		lsys_checks++;						\
		if (!(cond))						\
			lsys_fail(__FILE__, __LINE__, #cond);		\
	} while (0)

#endif /* LIBC_SHIM_H */
