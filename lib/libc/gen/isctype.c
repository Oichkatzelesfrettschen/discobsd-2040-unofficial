#include <ctype.h>

/*
 * The function forms of the table-driven classifiers.  A parenthesized
 * name is no macro invocation, so each definition names the function and
 * its body expands the macro of the same name.
 */

int
(isalnum)(int c)
{
	return isalnum(c);
}

int
(isalpha)(int c)
{
	return isalpha(c);
}

int
(iscntrl)(int c)
{
	return iscntrl(c);
}

int
(isdigit)(int c)
{
	return isdigit(c);
}

int
(isgraph)(int c)
{
	return isgraph(c);
}

int
(islower)(int c)
{
	return islower(c);
}

int
(isprint)(int c)
{
	return isprint(c);
}

int
(ispunct)(int c)
{
	return ispunct(c);
}

int
(isspace)(int c)
{
	return isspace(c);
}

int
(isupper)(int c)
{
	return isupper(c);
}

int
(isxdigit)(int c)
{
	return isxdigit(c);
}
