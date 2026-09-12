/*
 * See LICENSE file for copyright and license details.
 * LICENSE: usr.bin/textbox/LICENSE (sbase, MIT/X).
 * Adapted from sbase's mkfifo.c: this libc has no mkfifo(3), only the
 * mknod(2) syscall it is built on elsewhere (see chmod(1) and
 * mknod(8)), so the node is made directly with S_IFIFO in the mode.
 * The kernel's mknod() stores the type bit in the inode and nothing
 * else in sys/kern knows S_IFIFO, so the node passes no data through:
 * on the board a writer and a reader on it exchange nothing. The tool
 * builds so the day the kernel gains FIFOs it ships; until then the
 * rp2040 manifest leaves it out.
 */
#include <sys/stat.h>

#include <stdlib.h>

#include "compat.h"

static void
usage(void)
{
	eprintf("usage: %s [-m mode] name ...\n", argv0);
}

int
main(int argc, char *argv[])
{
	mode_t mode = 0666;
	int ret = 0;

	ARGBEGIN {
	case 'm':
		mode = parsemode(EARGF(usage()), mode, umask(0));
		break;
	default:
		usage();
	} ARGEND

	if (!argc)
		usage();

	for (; *argv; argc--, argv++) {
		if (mknod(*argv, (mode & ~S_IFMT) | S_IFIFO, 0) < 0) {
			weprintf("mkfifo %s:", *argv);
			ret = 1;
		}
	}

	return ret;
}
