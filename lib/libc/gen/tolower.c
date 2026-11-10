#include <ctype.h>

/*
 * C17 7.4.2.1: an uppercase letter maps to its lowercase letter and every
 * other argument is returned unchanged.  The range test keeps the function
 * independent of the _ctype_ table.
 */
int
tolower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}
