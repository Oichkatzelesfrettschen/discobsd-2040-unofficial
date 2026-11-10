#include <ctype.h>

/* C17 7.4.1.3: space and horizontal tab are the blanks of the C locale. */
int
isblank(int c)
{
	return c == ' ' || c == '\t';
}
