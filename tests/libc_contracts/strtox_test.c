#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

long db_strtol(const char *, char **, int);
unsigned long db_strtoul(const char *, char **, int);

static int checks;
static int failures;
static int invalid_ctype_arguments;

struct signed_case {
	const char *input;
	int base;
	long value;
	size_t end_offset;
	int host_comparable;
};

struct unsigned_case {
	const char *input;
	int base;
	unsigned long value;
	size_t end_offset;
	int host_comparable;
};

static void
check(int condition, const char *message)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "strtox test: %s\n", message);
	}
}

static void
check_case(int condition, const char *function, size_t case_index,
    const char *contract)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "strtox test: %s case %lu: %s\n", function,
		    (unsigned long)case_index, contract);
	}
}

static int
ctype_argument(int character)
{
	if (character < EOF || character > UCHAR_MAX) {
		invalid_ctype_arguments++;
		return EOF;
	}
	return character;
}

int
test_isalpha(int character)
{
	character = ctype_argument(character);
	return (character >= 'A' && character <= 'Z') ||
	    (character >= 'a' && character <= 'z');
}

int
test_isdigit(int character)
{
	character = ctype_argument(character);
	return character >= '0' && character <= '9';
}

int
test_isspace(int character)
{
	character = ctype_argument(character);
	return character == ' ' || character == '\t' || character == '\n' ||
	    character == '\v' || character == '\f' || character == '\r';
}

int
test_isupper(int character)
{
	character = ctype_argument(character);
	return character >= 'A' && character <= 'Z';
}

static void
signed_contracts(void)
{
	static const char high_bit_input[] = { (char)0x80, '1', '\0' };
	char maximum[64];
	char overflow[65];
	char *end;
	long value;

	invalid_ctype_arguments = 0;
	errno = 0;
	value = db_strtol(high_bit_input, &end, 10);
	check(value == 0 && end == high_bit_input,
	    "strtol high-bit input consumes no byte");
	check(errno == 0, "strtol high-bit input preserves errno");
	check(invalid_ctype_arguments == 0,
	    "strtol passes only EOF or unsigned-byte values to ctype");

	errno = 0;
	end = NULL;
	value = db_strtol("10", &end, 1);
	check(value == 0 && end != NULL && end[0] == '1',
	    "strtol invalid base consumes no byte");
	check(errno == EINVAL, "strtol invalid base reports EINVAL");

	errno = 0;
	value = db_strtol("0x", &end, 0);
	check(value == 0 && end != NULL && end[0] == 'x',
	    "strtol keeps zero as the largest valid prefix of 0x");
	check(errno == 0, "strtol incomplete hex prefix preserves errno");

	errno = 0;
	value = db_strtol(" -0x1fZ", &end, 0);
	check(value == -31 && end != NULL && end[0] == 'Z',
	    "strtol parses whitespace, sign, prefix, and end pointer");
	check(errno == 0, "strtol valid conversion preserves errno");

	(void)snprintf(maximum, sizeof(maximum), "%ld", LONG_MAX);
	(void)snprintf(overflow, sizeof(overflow), "%s0", maximum);
	errno = 0;
	value = db_strtol(overflow, &end, 10);
	check(value == LONG_MAX && errno == ERANGE && end[0] == '\0',
	    "strtol saturates positive overflow and consumes its digits");

	(void)snprintf(maximum, sizeof(maximum), "%ld", LONG_MIN);
	errno = 0;
	value = db_strtol(maximum, &end, 10);
	check(value == LONG_MIN && errno == 0 && end[0] == '\0',
	    "strtol accepts LONG_MIN without overflow");
}

static void
unsigned_contracts(void)
{
	static const char high_bit_input[] = { (char)0x80, '1', '\0' };
	char maximum[64];
	char overflow[65];
	char *end;
	unsigned long value;

	invalid_ctype_arguments = 0;
	errno = 0;
	value = db_strtoul(high_bit_input, &end, 10);
	check(value == 0 && end == high_bit_input,
	    "strtoul high-bit input consumes no byte");
	check(errno == 0, "strtoul high-bit input preserves errno");
	check(invalid_ctype_arguments == 0,
	    "strtoul passes only EOF or unsigned-byte values to ctype");

	errno = 0;
	end = NULL;
	value = db_strtoul("10", &end, 37);
	check(value == 0 && end != NULL && end[0] == '1',
	    "strtoul invalid base consumes no byte");
	check(errno == EINVAL, "strtoul invalid base reports EINVAL");

	errno = 0;
	value = db_strtoul("0xg", &end, 16);
	check(value == 0 && end != NULL && end[0] == 'x',
	    "strtoul keeps zero as the largest valid prefix of 0xg");
	check(errno == 0, "strtoul incomplete hex prefix preserves errno");

	errno = 0;
	value = db_strtoul("-10", &end, 10);
	check(value == 0UL - 10UL && end[0] == '\0',
	    "strtoul applies unsigned negation after conversion");
	check(errno == 0, "strtoul valid negative conversion preserves errno");

	(void)snprintf(maximum, sizeof(maximum), "%lu", ULONG_MAX);
	(void)snprintf(overflow, sizeof(overflow), "%s0", maximum);
	errno = 0;
	value = db_strtoul(overflow, &end, 10);
	check(value == ULONG_MAX && errno == ERANGE && end[0] == '\0',
	    "strtoul saturates overflow and consumes its digits");
}

static void
differential_contracts(void)
{
	static const struct signed_case signed_cases[] = {
		{ "0", 0, 0, 1, 1 },
		{ "077", 0, 63, 3, 1 },
		{ "0x1f", 0, 31, 4, 1 },
		{ "101", 2, 5, 3, 1 },
		{ "z", 36, 35, 1, 1 },
		{ "  +42tail", 10, 42, 5, 1 },
		{ "-17!", 10, -17, 3, 1 },
		{ "-", 10, 0, 0, 0 },
	};
	static const struct unsigned_case unsigned_cases[] = {
		{ "0", 0, 0, 1, 1 },
		{ "077", 0, 63, 3, 1 },
		{ "0Xff!", 0, 255, 4, 1 },
		{ "101", 2, 5, 3, 1 },
		{ "Z", 36, 35, 1, 1 },
		{ " +42tail", 10, 42, 4, 1 },
		{ "-10", 10, 0UL - 10UL, 3, 1 },
		{ "+", 10, 0, 0, 0 },
	};
	char *discobsd_end, *host_end;
	long discobsd_signed, host_signed;
	unsigned long discobsd_unsigned, host_unsigned;
	int discobsd_errno, host_errno;
	size_t case_index;

	for (case_index = 0;
	    case_index < sizeof(signed_cases) / sizeof(signed_cases[0]);
	    case_index++) {
		errno = 0;
		discobsd_signed = db_strtol(signed_cases[case_index].input,
		    &discobsd_end, signed_cases[case_index].base);
		discobsd_errno = errno;
		check_case(discobsd_signed == signed_cases[case_index].value &&
		    (size_t)(discobsd_end - signed_cases[case_index].input) ==
		    signed_cases[case_index].end_offset && discobsd_errno == 0,
		    "strtol", case_index, "matches the deterministic C17 oracle");
		if (!signed_cases[case_index].host_comparable)
			continue;
		errno = 0;
		host_signed = strtol(signed_cases[case_index].input, &host_end,
		    signed_cases[case_index].base);
		host_errno = errno;
		check_case(discobsd_signed == host_signed &&
		    discobsd_end - signed_cases[case_index].input ==
		    host_end - signed_cases[case_index].input &&
		    discobsd_errno == host_errno,
		    "strtol", case_index, "matches the host libc");
	}

	for (case_index = 0;
	    case_index < sizeof(unsigned_cases) / sizeof(unsigned_cases[0]);
	    case_index++) {
		errno = 0;
		discobsd_unsigned = db_strtoul(unsigned_cases[case_index].input,
		    &discobsd_end, unsigned_cases[case_index].base);
		discobsd_errno = errno;
		check_case(discobsd_unsigned == unsigned_cases[case_index].value &&
		    (size_t)(discobsd_end - unsigned_cases[case_index].input) ==
		    unsigned_cases[case_index].end_offset && discobsd_errno == 0,
		    "strtoul", case_index, "matches the deterministic C17 oracle");
		if (!unsigned_cases[case_index].host_comparable)
			continue;
		errno = 0;
		host_unsigned = strtoul(unsigned_cases[case_index].input, &host_end,
		    unsigned_cases[case_index].base);
		host_errno = errno;
		check_case(discobsd_unsigned == host_unsigned &&
		    discobsd_end - unsigned_cases[case_index].input ==
		    host_end - unsigned_cases[case_index].input &&
		    discobsd_errno == host_errno,
		    "strtoul", case_index, "matches the host libc");
	}
}

int
main(void)
{
	signed_contracts();
	unsigned_contracts();
	differential_contracts();
	if (failures != 0) {
		fprintf(stderr, "strtox test: %d of %d checks failed\n",
		    failures, checks);
		return EXIT_FAILURE;
	}
	printf("strtox test: %d checks passed\n", checks);
	return EXIT_SUCCESS;
}
