/*
 * Reporting for a gate that carries the tree's own userland headers.
 *
 * include/sys/types.h reads "typedef u_int size_t" and include/stdio.h spells
 * stderr as &_iob[2], so a translation unit reaching both the tree's headers
 * and the host's would take a four-byte size_t from one and an eight-byte one
 * from the other. The gate therefore reaches the tree's headers alone, and
 * this header names the only things it borrows from the host. umount_shim.c
 * is the one translation unit on the other side, and it carries no tree
 * header. The tier builds at ILP32, where the two size_t agree.
 */
#ifndef UMOUNT_SHIM_H
#define UMOUNT_SHIM_H

void	ushim_fail(const char *file, int line, const char *what);
void	ushim_note(const char *fmt, int a, int b);
int	ushim_verdict(const char *suite);

extern unsigned ushim_checks;
extern unsigned ushim_failures;

#define CHECK(cond)							\
	do {								\
		ushim_checks++;						\
		if (!(cond))						\
			ushim_fail(__FILE__, __LINE__, #cond);		\
	} while (0)

#endif /* UMOUNT_SHIM_H */
