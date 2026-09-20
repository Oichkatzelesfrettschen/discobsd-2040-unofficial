#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks;
static int failures;
int printf_test_errno;
struct _iobuf _iob[3];

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

	/*
	 * Length modifiers of C17 7.21.6.1p7. The formatter before this gate
	 * knew only l: %zu fell to the default case and printed the letters
	 * with the argument unconsumed, %lld read four bytes of an
	 * eight-byte argument, and %n printed a number.
	 */
	{
		char out[64];
		int count = -1;
		long lcount = -1;
		signed char ccount = -1;

		bounded_format(out, sizeof out, "%hhd %hd %hu", 200, 70000, 70000);
		check(strcmp(out, "-56 4464 4464") == 0,
		    "printf contract: hh and h narrow the promoted argument");
		bounded_format(out, sizeof out, "%lld|%llu|%llx",
		    -(1LL << 40), (unsigned long long)1 << 63, 0xdeadbeefcafeULL);
		check(strcmp(out, "-1099511627776|9223372036854775808|deadbeefcafe") == 0,
		    "printf contract: ll converts a 64-bit value");
		bounded_format(out, sizeof out, "%#llo", (unsigned long long)1 << 35);
		check(strcmp(out, "0400000000000") == 0,
		    "printf contract: # applies to a 64-bit octal");
		bounded_format(out, sizeof out, "%lld", 7LL);
		check(strcmp(out, "7") == 0,
		    "printf contract: a small ll value takes the word path");
		bounded_format(out, sizeof out, "%zu %zd %td %jd",
		    (size_t)1234567, (ssize_t)-5, (ptrdiff_t)-9, (long long)-3);
		check(strcmp(out, "1234567 -5 -9 -3") == 0,
		    "printf contract: z, t and j convert their types");
		bounded_format(out, sizeof out, "ab%ncd%lnef%hhn", &count, &lcount, &ccount);
		check(strcmp(out, "abcdef") == 0 && count == 2 && lcount == 4 && ccount == 6,
		    "printf contract: %n stores the count and prints nothing");
		bounded_format(out, sizeof out, "%.*s|%.*d|%5.3d", -1, "abc", -1, 7, 4);
		check(strcmp(out, "abc|7|  004") == 0,
		    "printf contract: a negative * precision is taken as omitted");
		bounded_format(out, sizeof out, "% d % d %+d", 5, -5, 5);
		check(strcmp(out, " 5 -5 +5") == 0,
		    "printf contract: the space flag reserves a sign position");
		bounded_format(out, sizeof out, "%#8.3llx|%#8.3x", 0ULL, 0);
		check(strcmp(out, "     000|     000") == 0,
		    "printf contract: # with a precision on zero adds no prefix");
		{
			char wide[640];
			int len = bounded_format(wide, sizeof wide, "%500.400lld|", 7LL);

			check(len == 501 && strlen(wide) == 501 && wide[99] == ' ' &&
			    wide[100] == '0' && wide[499] == '7',
			    "printf contract: a precision past the buffer still fills the field");
		}
		bounded_format(out, sizeof out, "%.0lld|%.0d|%.0x|%05.*d", 0LL, 0, 0, -1, 7);
		check(strcmp(out, "|||00007") == 0,
		    "printf contract: zero at zero precision prints no digit, and a negative * precision keeps the 0 flag");
		bounded_format(out, sizeof out, "%D", 123456789L);
		check(strcmp(out, "123456789") == 0,
		    "printf contract: the %D long extension survives");
	}
#ifdef PRINTF_FLOAT_TEST
	{
		char out[64];

		bounded_format(out, sizeof out, "%a %A", 1.0, 1.0);
		check(strcmp(out, "0x1p+0 0X1P+0") == 0,
		    "printf contract: %a of one");
		bounded_format(out, sizeof out, "%a", 0.1);
		check(strcmp(out, "0x1.999999999999ap-4") == 0,
		    "printf contract: %a prints every digit the value has");
		bounded_format(out, sizeof out, "%.3a %.0a", 1.0, 1.5);
		check(strcmp(out, "0x1.000p+0 0x2p+0") == 0 ||
		    strcmp(out, "0x1.000p+0 0x1p+1") == 0,
		    "printf contract: %a honors precision and rounds to even");
		bounded_format(out, sizeof out, "%.1a", 0x1.f8p0);
		check(strcmp(out, "0x1.0p+1") == 0,
		    "printf contract: %a carries a rounding overflow into the exponent");
		bounded_format(out, sizeof out, "%a %a", -0.0, 0.0);
		check(strcmp(out, "-0x0p+0 0x0p+0") == 0,
		    "printf contract: %a keeps the sign of a negative zero");
		bounded_format(out, sizeof out, "%020a|%-10a|", 1.0, 1.0);
		check(strcmp(out, "0x000000000000001p+0|0x1p+0    |") == 0,
		    "printf contract: %a zero padding goes after the 0x prefix");
		{
			volatile double zero = 0.0;
			double inf = 1.0 / zero, nan = zero / zero;

			bounded_format(out, sizeof out, "%a %A %f %F %e %G", inf, inf, nan, nan, -inf, nan);
			check(strcmp(out, "inf INF nan NAN -inf NAN") == 0,
			    "printf contract: inf and nan follow the conversion's case");
		}
		bounded_format(out, sizeof out, "%.14a", 1.875);
		check(strcmp(out, "0x1.e0000000000000p+0") == 0,
		    "printf contract: %a defers extra precision zeros to the exponent, not to a mantissa e");
		{
			int len = bounded_format(out, sizeof out, "%30.20a|", 1.0);

			check(len == 31 && out[0] == ' ' && out[2] == ' ' && out[3] == '0' && out[30] == '|',
			    "printf contract: %a counts extended precision in its field width");
		}
		bounded_format(out, sizeof out, "%.2f %e", 3.14159, 1234.5);
		check(strcmp(out, "3.14 1.234500e+03") == 0,
		    "printf contract: %f and %e still convert");
	}
#endif

	if (failures != 0) {
		(void)write(2, "printf contract tests failed\n", 29);
		return 1;
	}
	(void)write(1, "printf contract tests passed\n", 29);
	return 0;
}
