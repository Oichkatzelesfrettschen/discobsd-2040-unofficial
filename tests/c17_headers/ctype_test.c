/*
 * The tree's <ctype.h> with lib/libc/gen/ctype_.c, isctype.c, isblank.c,
 * tolower.c, toupper.c and isascii.c, linked under the db_ names that
 * ctype_rename.h assigns.  The oracle is the C locale of C17 7.4 written
 * as byte ranges, independent of the table and of the hosting library.
 */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>

static unsigned long checks;
static unsigned long failures;

static void
expect(int condition, const char *name, int argument)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "ctype test: %s(%d)\n", name, argument);
	}
}

static int
o_upper(int c)
{
	return c >= 'A' && c <= 'Z';
}

static int
o_lower(int c)
{
	return c >= 'a' && c <= 'z';
}

static int
o_digit(int c)
{
	return c >= '0' && c <= '9';
}

static int
o_alpha(int c)
{
	return o_upper(c) || o_lower(c);
}

static int
o_alnum(int c)
{
	return o_alpha(c) || o_digit(c);
}

static int
o_xdigit(int c)
{
	return o_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int
o_space(int c)
{
	return c == ' ' || (c >= '\t' && c <= '\r');
}

static int
o_blank(int c)
{
	return c == ' ' || c == '\t';
}

static int
o_graph(int c)
{
	return c > ' ' && c < 0177;
}

static int
o_print(int c)
{
	return c >= ' ' && c < 0177;
}

static int
o_punct(int c)
{
	return o_graph(c) && !o_alnum(c);
}

static int
o_cntrl(int c)
{
	return (c >= 0 && c < ' ') || c == 0177;
}

typedef int ctype_function(int);

/* Named function pointers, so no macro stands between the test and libc. */
static ctype_function *const f_alnum = isalnum;
static ctype_function *const f_alpha = isalpha;
static ctype_function *const f_blank = isblank;
static ctype_function *const f_cntrl = iscntrl;
static ctype_function *const f_digit = isdigit;
static ctype_function *const f_graph = isgraph;
static ctype_function *const f_lower = islower;
static ctype_function *const f_print = isprint;
static ctype_function *const f_punct = ispunct;
static ctype_function *const f_space = isspace;
static ctype_function *const f_upper = isupper;
static ctype_function *const f_xdigit = isxdigit;
static ctype_function *const f_tolower = tolower;
static ctype_function *const f_toupper = toupper;
static ctype_function *const f_isascii = isascii;
static ctype_function *const f_toascii = toascii;

/* The macro, the parenthesized function and the pointer agree with o. */
#define	CLASS(name, oracle, pointer, c) do { \
	expect(!name(c) == !oracle(c), #name " macro", (c)); \
	expect(!(name)(c) == !oracle(c), #name " function", (c)); \
	expect(!pointer(c) == !oracle(c), #name " pointer", (c)); \
} while (0)

/* EOF and every unsigned char value: the whole domain of C17 7.4p1. */
static void
domain(void)
{
	int c, upper, lower;

	for (c = -1; c <= 255; c++) {
		CLASS(isalnum, o_alnum, f_alnum, c);
		CLASS(isalpha, o_alpha, f_alpha, c);
		CLASS(iscntrl, o_cntrl, f_cntrl, c);
		CLASS(isdigit, o_digit, f_digit, c);
		CLASS(isgraph, o_graph, f_graph, c);
		CLASS(islower, o_lower, f_lower, c);
		CLASS(isprint, o_print, f_print, c);
		CLASS(ispunct, o_punct, f_punct, c);
		CLASS(isspace, o_space, f_space, c);
		CLASS(isupper, o_upper, f_upper, c);
		CLASS(isxdigit, o_xdigit, f_xdigit, c);
		expect(!isblank(c) == !o_blank(c), "isblank", c);
		expect(!f_blank(c) == !o_blank(c), "isblank pointer", c);
		upper = o_lower(c) ? c - 'a' + 'A' : c;
		lower = o_upper(c) ? c - 'A' + 'a' : c;
		expect(toupper(c) == upper, "toupper", c);
		expect(f_toupper(c) == upper, "toupper pointer", c);
		expect(tolower(c) == lower, "tolower", c);
		expect(f_tolower(c) == lower, "tolower pointer", c);
	}
	/* The two 7-bit forms accept every int. */
	for (c = -600; c <= 600; c++) {
		expect(!isascii(c) == !(c >= 0 && c <= 0177), "isascii macro", c);
		expect(!f_isascii(c) == !(c >= 0 && c <= 0177), "isascii", c);
		expect(toascii(c) == (c & 0177), "toascii macro", c);
		expect(f_toascii(c) == (c & 0177), "toascii", c);
	}
}

/*
 * C17 7.1.4p1: an argument with a side effect takes effect once.  Each
 * name runs over arguments inside and outside every class, because a macro
 * built from two tests joined by || or && repeats its argument only on the
 * side that does not short-circuit.
 */
static const int once_arguments[] = { 0, '\t', ' ', '!', '0', 'A', 'a', 0177, 200 };

#define	ONCE(name) do { \
	size_t index; \
	for (index = 0; index < sizeof(once_arguments) / \
	    sizeof(once_arguments[0]); index++) { \
		int probe = once_arguments[index]; \
		(void)name(probe++); \
		expect(probe == once_arguments[index] + 1, \
		    #name " evaluates its argument once", probe); \
	} \
} while (0)

static void
single_evaluation(void)
{
	ONCE(isalnum);
	ONCE(isalpha);
	ONCE(isblank);
	ONCE(iscntrl);
	ONCE(isdigit);
	ONCE(isgraph);
	ONCE(islower);
	ONCE(isprint);
	ONCE(ispunct);
	ONCE(isspace);
	ONCE(isupper);
	ONCE(isxdigit);
	ONCE(tolower);
	ONCE(toupper);
	ONCE(isascii);
	ONCE(toascii);
}

int
main(void)
{
	domain();
	single_evaluation();
	if (failures != 0) {
		fprintf(stderr, "ctype test: %lu of %lu checks failed\n",
		    failures, checks);
		return 1;
	}
	printf("ctype test: %lu checks passed\n", checks);
	return 0;
}
