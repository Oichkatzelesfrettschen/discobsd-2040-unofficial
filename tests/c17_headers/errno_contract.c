#include <errno.h>

/*
 * C17 7.5p2: EDOM, EILSEQ and ERANGE expand to distinct positive integer
 * constant expressions usable in #if.
 */
#if !defined(EDOM) || !defined(EILSEQ) || !defined(ERANGE)
#error "C17 7.5 requires EDOM, EILSEQ and ERANGE"
#endif
#if EDOM <= 0 || EILSEQ <= 0 || ERANGE <= 0
#error "the C17 7.5 error numbers are positive"
#endif
#if EDOM == EILSEQ || EDOM == ERANGE || EILSEQ == ERANGE
#error "the C17 7.5 error numbers are distinct"
#endif
_Static_assert(_Generic(EILSEQ, int: 1, default: 0), "EILSEQ has type int");

int errno_probe(void);

int
errno_probe(void)
{
	return errno;
}
