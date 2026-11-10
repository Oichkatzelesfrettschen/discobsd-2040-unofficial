/*
 * lib/libc/stdlib/strtoull.c, strtoll.c and atoll.c compiled under db_
 * names and compared with the hosting C library, which stays available
 * under the standard names as the oracle.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long long db_strtoll(const char *, char **, int);
unsigned long long db_strtoull(const char *, char **, int);
long long db_atoll(const char *);

static unsigned long checks;
static unsigned long failures;

static void
expect(int condition, const char *contract)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "strtoll test: %s\n", contract);
	}
}

/*
 * Two prefix shapes where hosting libraries differ and C17 7.22.1.4 gives
 * one answer: the subject sequence is the longest initial subsequence of
 * the expected form, which is the zero alone, so the value is 0 and the end
 * pointer names the letter after it.
 *
 *   "0x" without a hex digit, base 0 or 16: newlib consumes no byte.
 *   "0b", base 0 or 2: a C23 library converts the binary digits after it,
 *   and C17 has no such prefix.
 *
 * Both are held to the standard's answer and kept away from the oracle.
 * Returns the required end pointer, or NULL for any other input.
 */
static const char *
c17_prefix_end(const char *input, int base)
{
	const unsigned char *cursor = (const unsigned char *)input;

	while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r'))
		cursor++;
	if (*cursor == '+' || *cursor == '-')
		cursor++;
	if (cursor[0] != '0')
		return NULL;
	if ((base == 0 || base == 2) &&
	    (cursor[1] == 'b' || cursor[1] == 'B'))
		return (const char *)cursor + 1;
	if ((base != 0 && base != 16) ||
	    (cursor[1] != 'x' && cursor[1] != 'X'))
		return NULL;
	if ((cursor[2] >= '0' && cursor[2] <= '9') ||
	    (cursor[2] >= 'a' && cursor[2] <= 'f') ||
	    (cursor[2] >= 'A' && cursor[2] <= 'F'))
		return NULL;
	return (const char *)cursor + 1;
}

static unsigned long pinned_prefixes;

/*
 * C17 7.22.1.4p7 gives a subject sequence without digits the value zero
 * and the end pointer of the input, and says nothing of errno; POSIX lets
 * an implementation report EINVAL there, and the BSD-derived libraries do.
 * The tree leaves errno untouched, so for that case the expected errno is
 * the tree's contract and the oracle supplies value and end pointer alone.
 */
static int
no_conversion_errno(const char *input, const char *end, int oracle_errno)
{
	return end == input ? 0 : oracle_errno;
}

static void
compare(const char *input, int base)
{
	char *tree_end, *host_end;
	const char *required_end;
	long long tree_signed, host_signed;
	unsigned long long tree_unsigned, host_unsigned;
	int tree_errno, host_errno;

	required_end = c17_prefix_end(input, base);
	if (required_end != NULL) {
		pinned_prefixes++;
		errno = 0;
		tree_signed = db_strtoll(input, &tree_end, base);
		expect(tree_signed == 0 && tree_end == required_end &&
		    errno == 0, "strtoll keeps the zero of a 0x or 0b prefix");
		errno = 0;
		tree_unsigned = db_strtoull(input, &tree_end, base);
		expect(tree_unsigned == 0 && tree_end == required_end &&
		    errno == 0, "strtoull keeps the zero of a 0x or 0b prefix");
		return;
	}

	errno = 0;
	host_signed = strtoll(input, &host_end, base);
	host_errno = no_conversion_errno(input, host_end, errno);
	errno = 0;
	tree_signed = db_strtoll(input, &tree_end, base);
	tree_errno = errno;
	checks++;
	if (tree_signed != host_signed || tree_end != host_end ||
	    tree_errno != host_errno) {
		failures++;
		fprintf(stderr, "strtoll(\"%s\", %d): tree %lld end %ld errno %d, "
		    "oracle %lld end %ld errno %d\n", input, base, tree_signed,
		    (long)(tree_end - input), tree_errno, host_signed,
		    (long)(host_end - input), host_errno);
	}

	errno = 0;
	host_unsigned = strtoull(input, &host_end, base);
	host_errno = no_conversion_errno(input, host_end, errno);
	errno = 0;
	tree_unsigned = db_strtoull(input, &tree_end, base);
	tree_errno = errno;
	checks++;
	if (tree_unsigned != host_unsigned || tree_end != host_end ||
	    tree_errno != host_errno) {
		failures++;
		fprintf(stderr, "strtoull(\"%s\", %d): tree %llu end %ld errno %d, "
		    "oracle %llu end %ld errno %d\n", input, base, tree_unsigned,
		    (long)(tree_end - input), tree_errno, host_unsigned,
		    (long)(host_end - input), host_errno);
	}
}

/* Writes value in base, most significant digit first, and returns text. */
static char *
render(char *text, unsigned long long value, int base, int upper)
{
	static const char lower_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	static const char upper_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char reversed[65];
	int count = 0;

	do {
		reversed[count++] = (upper ? upper_digits : lower_digits)
		    [value % (unsigned)base];
		value /= (unsigned)base;
	} while (value != 0);
	while (count > 0)
		*text++ = reversed[--count];
	*text = '\0';
	return text;
}

/*
 * Every base gets the values around each limit and around each limb carry,
 * rendered exactly and with one more digit appended, under each sign.
 */
static void
boundary_corpus(void)
{
	static const unsigned long long anchors[] = {
		0ULL, 1ULL, 35ULL, 36ULL, 0xffffULL, 0x10000ULL,
		0xffffffffULL, 0x100000000ULL, 0xffffffffffffULL,
		0x1000000000000ULL, 0x7ffffffffffffffeULL,
		0x7fffffffffffffffULL, 0x8000000000000000ULL,
		0x8000000000000001ULL, 0xfffffffffffffffeULL,
		0xffffffffffffffffULL
	};
	static const char *const prefixes[] = { "", "+", "-", " \t\n\v\f\r-" };
	char text[96], *end;
	size_t anchor, prefix;
	int base, digit;

	for (base = 2; base <= 36; base++) {
		for (anchor = 0; anchor < sizeof(anchors) / sizeof(anchors[0]);
		    anchor++) {
			for (prefix = 0;
			    prefix < sizeof(prefixes) / sizeof(prefixes[0]);
			    prefix++) {
				strcpy(text, prefixes[prefix]);
				end = render(text + strlen(text),
				    anchors[anchor], base, (int)(anchor & 1));
				compare(text, base);
				for (digit = 0; digit < base; digit += base - 1) {
					end[0] = digit < 10 ? (char)('0' + digit) :
					    (char)('a' + digit - 10);
					end[1] = '\0';
					compare(text, base);
				}
				/* The first byte outside the base ends the scan. */
				end[0] = base < 10 ? (char)('0' + base) :
				    base < 36 ? (char)('a' + base - 10) : '!';
				end[1] = '7';
				end[2] = '\0';
				compare(text, base);
			}
		}
	}
}

static void
fixed_corpus(void)
{
	static const char *const inputs[] = {
		"", " ", "-", "+", "+-1", "--1", "0", "00", "-0", "0x", "0X",
		"0xg", "0x1", "0X1f", "-0x10", "+0x7fffffffffffffff",
		"0x8000000000000000", "-0x8000000000000000",
		"-0x8000000000000001", "0xffffffffffffffff",
		"0x10000000000000000", "-0xffffffffffffffff",
		"-0x10000000000000000", "017", "08", "0b1", "x1", "z", "Zz",
		"9223372036854775807", "9223372036854775808",
		"-9223372036854775808", "-9223372036854775809",
		"18446744073709551615", "18446744073709551616",
		"-18446744073709551615", "-18446744073709551616",
		"99999999999999999999999999999999999999xyz",
		"-99999999999999999999999999999999999999 1",
		"  \t42tail", "1 2", "1e5", "\2001", "1\200"
	};
	static const int bases[] = { 0, 2, 8, 10, 16, 33, 36 };
	size_t input, base;

	for (input = 0; input < sizeof(inputs) / sizeof(inputs[0]); input++)
		for (base = 0; base < sizeof(bases) / sizeof(bases[0]); base++)
			compare(inputs[input], bases[base]);
}

/* The state stays in 32 bits, so every host walks the same corpus. */
static unsigned long
step(unsigned long state)
{
	return (state * 1103515245UL + 12345UL) & 0xffffffffUL;
}

/* A linear congruential walk over digit strings of every length. */
static void
generated_corpus(void)
{
	static const char alphabet[] = "0123456789abcdefABCDEFxXzZ+- \tg";
	char text[40];
	unsigned long state = 2463534242UL;
	int round, length, index, base;

	for (round = 0; round < 20000; round++) {
		state = step(state);
		length = (int)((state >> 16) % 24);
		for (index = 0; index < length; index++) {
			state = step(state);
			text[index] = alphabet[(state >> 16) %
			    (sizeof(alphabet) - 1)];
		}
		text[length] = '\0';
		state = step(state);
		base = (int)((state >> 16) % 37);
		if (base == 1)
			base = 0;
		compare(text, base);
	}
}

/*
 * The standard leaves the result of an unsupported base to the
 * implementation, so these are the tree's own contract and no oracle run.
 */
static void
invalid_base(void)
{
	static const int bases[] = { -1, 1, 37, INT_MAX, INT_MIN };
	char *end;
	size_t base;

	for (base = 0; base < sizeof(bases) / sizeof(bases[0]); base++) {
		errno = 0;
		end = NULL;
		expect(db_strtoll("12", &end, bases[base]) == 0 &&
		    end != NULL && *end == '1' && errno == EINVAL,
		    "strtoll invalid base is 0, EINVAL, no byte consumed");
		errno = 0;
		end = NULL;
		expect(db_strtoull("12", &end, bases[base]) == 0 &&
		    end != NULL && *end == '1' && errno == EINVAL,
		    "strtoull invalid base is 0, EINVAL, no byte consumed");
	}
	errno = 0;
	expect(db_strtoll("-5", NULL, 10) == -5 && errno == 0,
	    "strtoll accepts a null end pointer");
	expect(db_strtoull("5", NULL, 10) == 5 && errno == 0,
	    "strtoull accepts a null end pointer");
	expect(db_atoll("  -9223372036854775808x") == LLONG_MIN,
	    "atoll converts the most negative value");
	expect(db_atoll("77") == 77 && db_atoll("z") == 0,
	    "atoll converts decimal and returns 0 without digits");
}

int
main(void)
{
	boundary_corpus();
	fixed_corpus();
	generated_corpus();
	invalid_base();
	if (failures != 0) {
		fprintf(stderr, "strtoll test: %lu of %lu checks failed\n",
		    failures, checks);
		return 1;
	}
	/* A corpus without the pinned shape would leave it unmeasured. */
	if (pinned_prefixes == 0) {
		fprintf(stderr, "strtoll test: no pinned prefix input ran\n");
		return 1;
	}
	printf("strtoll test: %lu checks passed, %lu prefix inputs held to "
	    "C17 7.22.1.4\n", checks, pinned_prefixes);
	return 0;
}
