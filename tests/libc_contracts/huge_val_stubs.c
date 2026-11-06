/*
 * The tree's atof and strtod classify characters through the isdigit,
 * isspace, isalpha and isupper macros of strtox_include/ctype.h, and the
 * tree libm reports through an errno the host library does not own. Both
 * are provided here for the tree objects of huge_val_contract_test.
 */
int tree_errno;

int
test_isdigit(int c)
{
	return c >= '0' && c <= '9';
}

int
test_isspace(int c)
{
	return c == ' ' || (c >= '\t' && c <= '\r');
}

int
test_isalpha(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int
test_isupper(int c)
{
	return c >= 'A' && c <= 'Z';
}
