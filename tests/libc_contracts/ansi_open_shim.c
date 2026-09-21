/*
 * The open(2) the tree's fopen, freopen and fdopen reach under the ANSI
 * stdio gate, translating the flags they pass into the host's.
 *
 * This is a translation unit of its own because the two sets of headers
 * cannot meet: the gate's other unit compiles against the tree's <fcntl.h>,
 * whose O_CREAT is 0x0200 and O_TRUNC 0x0400, while the host kernel wants
 * its own bits, which are 0x0040 and 0x0200 on Linux and the BSD values on
 * Darwin. Only the host's names are in scope here, so the tree's four bits
 * are spelled out below; ansi_contract_test.c asserts each against the
 * tree's own macro, where those are the ones in scope.
 */
#include <fcntl.h>
#include <stdarg.h>

#define TREE_O_ACCMODE  0x0003	/* O_RDONLY, O_WRONLY and O_RDWR */
#define TREE_O_APPEND   0x0008
#define TREE_O_CREAT    0x0200
#define TREE_O_TRUNC    0x0400
#define TREE_O_EXCL     0x0800

int ansi_open_flags;		/* the tree flags of the last call */

int
test_open(const char *path, int flags, ...)
{
	va_list ap;
	int host, mode;

	va_start(ap, flags);
	mode = va_arg(ap, int);
	va_end(ap);

	ansi_open_flags = flags;
	host = flags & TREE_O_ACCMODE;
	if (flags & TREE_O_CREAT)
		host |= O_CREAT;
	if (flags & TREE_O_TRUNC)
		host |= O_TRUNC;
	if (flags & TREE_O_APPEND)
		host |= O_APPEND;
	if (flags & TREE_O_EXCL)
		host |= O_EXCL;
	return (open(path, host, mode));
}
