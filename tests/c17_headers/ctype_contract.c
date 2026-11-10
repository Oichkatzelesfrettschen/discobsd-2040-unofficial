#include <ctype.h>
#include <ctype.h>

/*
 * C17 7.1.4p1: each function is a real one whose name may be taken as a
 * pointer, parenthesized or #undef'd.  The second inclusion above checks
 * the include guard.
 */
typedef int ctype_function(int);

int parenthesized_probe(int);
int undefined_probe(int);
ctype_function *pointer_probe(unsigned);

static ctype_function *const pointers[] = {
	isalnum, isalpha, isblank, iscntrl, isdigit, isgraph, islower,
	isprint, ispunct, isspace, isupper, isxdigit, tolower, toupper,
	isascii, toascii
};

ctype_function *
pointer_probe(unsigned index)
{
	return pointers[index % (sizeof(pointers) / sizeof(pointers[0]))];
}

int
parenthesized_probe(int c)
{
	return (isalnum)(c) + (isalpha)(c) + (isblank)(c) + (iscntrl)(c) +
	    (isdigit)(c) + (isgraph)(c) + (islower)(c) + (isprint)(c) +
	    (ispunct)(c) + (isspace)(c) + (isupper)(c) + (isxdigit)(c) +
	    (tolower)(c) + (toupper)(c) + (isascii)(c) + (toascii)(c);
}

/* The table is constant storage, so a write to it fails translation. */
_Static_assert(_Generic(&_ctype_[0], const char *: 1, default: 0),
    "_ctype_ is const");

#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef isascii
#undef toascii

int
undefined_probe(int c)
{
	return isalnum(c) + isalpha(c) + isblank(c) + iscntrl(c) +
	    isdigit(c) + isgraph(c) + islower(c) + isprint(c) + ispunct(c) +
	    isspace(c) + isupper(c) + isxdigit(c) + tolower(c) + toupper(c) +
	    isascii(c) + toascii(c);
}
