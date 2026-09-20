#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks;
static int failures;
int printf_test_errno;

/* The indirect call keeps the compiler from replacing the function under test
 * with its builtin or rejecting deliberate truncation before the gate runs. */
static int (*volatile bounded_format)(char *, size_t, const char *, ...) =
    snprintf;

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		(void)write(2, message, strlen(message));
		(void)write(2, "\n", 1);
	}
}

static int
format_with_va_list(char *buffer, size_t buffer_size, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(buffer, buffer_size, format, arguments);
	va_end(arguments);
	return length;
}

int
main(void)
{
	struct guarded_buffer {
		unsigned char before;
		char text[4];
		unsigned char after;
	} guarded = { 0xa5, { 0, 0, 0, 0 }, 0x5a };
	char exact[4];
	char one[1] = { 'x' };
	char untouched = 'x';
	int length;

	length = bounded_format(guarded.text, sizeof(guarded.text), "%s", "abcdef");
	check(length == 6, "printf contract: snprintf reports required length");
	check(strcmp(guarded.text, "abc") == 0,
	    "printf contract: snprintf terminates truncated output");
	check(guarded.before == 0xa5 && guarded.after == 0x5a,
	    "printf contract: snprintf preserves destination guards");

	length = bounded_format(exact, sizeof(exact), "%s", "abc");
	check(length == 3 && strcmp(exact, "abc") == 0,
	    "printf contract: snprintf accepts an exact fit");

	length = bounded_format(exact, sizeof(exact), "%m");
	check(length == 2 && strcmp(exact, "%m") == 0,
	    "printf contract: percent-m stays literal outside syslog");

	length = bounded_format(one, sizeof(one), "%c", 'z');
	check(length == 1 && one[0] == '\0',
	    "printf contract: size-one output contains only the terminator");

	length = bounded_format(&untouched, 0, "%s", "zero");
	check(length == 4 && untouched == 'x',
	    "printf contract: zero size measures without writing");
	check(bounded_format(NULL, 0, "%04d", 12) == 4,
	    "printf contract: null zero-size destination measures output");

	memset(guarded.text, 0, sizeof(guarded.text));
	length = format_with_va_list(guarded.text, sizeof(guarded.text),
	    "%s", "abcdef");
	check(length == 6 && strcmp(guarded.text, "abc") == 0,
	    "printf contract: vsnprintf reports and terminates truncation");
	check(guarded.before == 0xa5 && guarded.after == 0x5a,
	    "printf contract: vsnprintf preserves destination guards");

	errno = 0;
	length = bounded_format(exact, (size_t)INT_MAX + 1U, "%s", "x");
	check(length == -1 && errno == EINVAL,
	    "printf contract: unrepresentable stream size reports EINVAL");

	if (failures != 0) {
		(void)write(2, "printf contract tests failed\n", 29);
		return 1;
	}
	(void)write(1, "printf contract tests passed\n", 29);
	return 0;
}
