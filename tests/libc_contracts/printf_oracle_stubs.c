/*
 * The tree stdio state that lib/libc/stdio/flsbuf.c and doprnt.c reach,
 * compiled against the tree headers with the stream names moved aside
 * (see PRINTF_ORACLE_RENAMES in the Makefile) so a host libc keeps its own
 * stdio and serves as the oracle for the tree formatter.
 */
#include <stdio.h>

int printf_test_errno;
struct _iobuf _iob[3];

/*
 * fflush(NULL) walks every open stream; the formatter tests write only
 * to string streams, so the walk has nothing to visit.
 */
int
_fwalk(int (*function)(FILE *))
{
	(void)function;
	return (0);
}
