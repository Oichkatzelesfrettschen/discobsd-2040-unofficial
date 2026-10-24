#ifndef NAMEI_SOURCE
#define NAMEI_SOURCE "../../../sys/kern/ufs_namei.c"
#endif
#include NAMEI_SOURCE
#include <sys/namei.h>

static u_long user_limit;

static void
test_bytes_copy(caddr_t destination, const caddr_t source, u_int length)
{
	u_int index;

	for (index = 0; index < length; index++)
		destination[index] = source[index];
}

static int
test_strings_equal(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

int
copyin(caddr_t source, caddr_t destination, u_int length)
{
	u_long address = (u_long)source;

	if (address < 4096 || (user_limit != 0 &&
	    (address >= user_limit || length > user_limit - address)))
		return EFAULT;
	test_bytes_copy(destination, source, length);
	return 0;
}

int
copystr(caddr_t source, caddr_t destination, u_int maximum, u_int *copied)
{
	u_int length;

	for (length = 0; length < maximum; length++) {
		destination[length] = source[length];
		if (source[length] == '\0') {
			if (copied != NULL)
				*copied = length + 1;
			return 0;
		}
	}
	if (copied != NULL)
		*copied = maximum;
	return ENOENT;
}

static int
check_user_path(void)
{
	struct nameidata name;
	char source[MAXPATHLEN] = "/usr/bin/tool";
	char destination[MAXPATHLEN];

	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, source);
	if ((name.ni_nameiop & NI_USERPATH) == 0)
		return 1;
	user_limit = 0;
	if (namei_copy_path(&name, destination) != 0 ||
	    !test_strings_equal(destination, source))
		return 2;
	return 0;
}

static int
check_invalid_user_path(void)
{
	struct nameidata name;
	char destination[MAXPATHLEN];

	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, (caddr_t)1);
	if (namei_copy_path(&name, destination) != EFAULT)
		return 1;
	return 0;
}

static int
check_user_boundary_terminator(void)
{
	struct nameidata name;
	char source[MAXPATHLEN] = "/abc";
	char destination[MAXPATHLEN];

	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, source);
	user_limit = (u_long)source + sizeof "/abc";
	if (namei_copy_path(&name, destination) != 0 ||
	    !test_strings_equal(destination, "/abc"))
		return 1;
	return 0;
}

static int
check_missing_terminator_at_boundary(void)
{
	struct nameidata name;
	char source[MAXPATHLEN] = "abcde";
	char destination[MAXPATHLEN];

	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, source);
	user_limit = (u_long)source + 5;
	if (namei_copy_path(&name, destination) != EFAULT)
		return 1;
	return 0;
}

static int
check_maximum_length_path(void)
{
	struct nameidata name;
	char source[MAXPATHLEN];
	char destination[MAXPATHLEN];
	u_int index;

	for (index = 0; index < MAXPATHLEN; index++)
		source[index] = 'x';
	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, source);
	user_limit = 0;
	if (namei_copy_path(&name, destination) != ENOENT)
		return 1;
	return 0;
}

static int
check_kernel_path(void)
{
	struct nameidata name;
	char source[MAXPATHLEN] = "/core.core";
	char destination[MAXPATHLEN];

	NAMEI_INIT_KERNEL(&name, CREATE, FOLLOW, source);
	if (name.ni_nameiop & NI_USERPATH)
		return 1;
	if (namei_copy_path(&name, destination) != 0 ||
	    !test_strings_equal(destination, source))
		return 2;
	return 0;
}

static int
check_operation_reset(void)
{
	struct nameidata name;
	char user_path[MAXPATHLEN] = "/user";
	char kernel_path[MAXPATHLEN] = "/kernel";

	NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, user_path);
	NAMEI_REJECT_EMPTY(&name);
	NAMEI_RESET_OPERATION(&name, CREATE, NOFOLLOW);
	if (name.ni_dirp != user_path ||
	    name.ni_nameiop != (CREATE | NI_USERPATH | NI_NOEMPTY))
		return 1;

	NAMEI_INIT_KERNEL(&name, CREATE, NOFOLLOW, kernel_path);
	NAMEI_RESET_OPERATION(&name, LOOKUP, FOLLOW);
	if (name.ni_dirp != kernel_path ||
	    name.ni_nameiop != (LOOKUP | FOLLOW))
		return 2;
	return 0;
}

int
main(void)
{
	int error;

	error = check_user_path();
	if (error != 0)
		return 10 + error;
	error = check_user_boundary_terminator();
	if (error != 0)
		return 30 + error;
	error = check_missing_terminator_at_boundary();
	if (error != 0)
		return 40 + error;
	error = check_invalid_user_path();
	if (error != 0)
		return 20 + error;
	error = check_maximum_length_path();
	if (error != 0)
		return 45 + error;
	error = check_kernel_path();
	if (error != 0)
		return 50 + error;
	error = check_operation_reset();
	if (error != 0)
		return 60 + error;
	return 0;
}
