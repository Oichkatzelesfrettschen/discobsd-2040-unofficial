/*
 * seq(1): print a sequence of integers from first to last, by step.
 * sbase's seq.c parses start/step/end with strtod() and formats each
 * term through printf("%f"-family), which this port refuses: a float
 * in printf costs about 10 KB of _doprnt's cvt path (see STORAGE.md),
 * whether or not the program prints one, on every program that links
 * it. This is a fresh integer-only implementation instead of an
 * import: start, step and end are long, the increment loop is
 * integer, and -w zero-pads with the widest term's decimal digit
 * count rather than sbase's -f/-w printf-format machinery.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

static int
numdigits(long v)
{
	int n = 1;

	if (v < 0)
		v = (v == LONG_MIN) ? LONG_MAX : -v;
	while (v >= 10) {
		v /= 10;
		n++;
	}
	return n;
}

static void
usage(void)
{
	eprintf("usage: %s [-s sep] [-w] [first [step]] last\n", argv0);
}

int
main(int argc, char *argv[])
{
	long first = 1, step = 1, last, out;
	int wflag = 0, width, ret;
	char *sep = "\n";

	ARGBEGIN {
	case 's':
		sep = EARGF(usage());
		break;
	case 'w':
		wflag = 1;
		break;
	default:
		usage();
	} ARGEND

	switch (argc) {
	case 3:
		step = estrtonum(argv[1], LONG_MIN, LONG_MAX);
		if (step == 0)
			eprintf("step cannot be 0\n");
		argv[1] = argv[2]; /* so the case 1 fallthrough sees last */
		/* fallthrough */
	case 2:
		first = estrtonum(argv[0], LONG_MIN, LONG_MAX);
		argv++;
		/* fallthrough */
	case 1:
		last = estrtonum(argv[0], LONG_MIN, LONG_MAX);
		break;
	default:
		usage();
		return 1; /* unreached: usage() calls eprintf(), which exits */
	}

	if (step == 0)
		eprintf("step cannot be 0\n");
	if ((step > 0 && first > last) || (step < 0 && first < last))
		return 0;

	width = 0;
	if (wflag)
		width = MAX(numdigits(first), numdigits(last));

	for (out = first; (step > 0) ? (out <= last) : (out >= last); out += step) {
		if (out != first)
			fputs(sep, stdout);
		if (width)
			printf("%0*ld", width, out);
		else
			printf("%ld", out);
		/* stop before wraparound would turn the loop infinite */
		if (step > 0 && out > LONG_MAX - step)
			break;
		if (step < 0 && out < LONG_MIN - step)
			break;
	}
	putchar('\n');

	ret = fshut(stdout, "<stdout>");
	return ret;
}
