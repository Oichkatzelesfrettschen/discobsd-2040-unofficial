#include <ctype.h>

/*
 * C17 7.4.2.2: a lowercase letter maps to its uppercase letter and every
 * other argument is returned unchanged.  The range test keeps the function
 * independent of the _ctype_ table.
 */
int
toupper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}
