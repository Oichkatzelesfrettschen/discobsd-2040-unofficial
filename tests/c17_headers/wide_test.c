/*
 * lib/libc/stdlib's C-locale multibyte functions and wide integer
 * conversions, compiled under db_ names.
 *
 * The multibyte cases are derived from C17 7.22.7 and 7.22.8 over the
 * encoding the library documents: one byte per character, the wide value
 * equal to the byte read as unsigned char.  A hosting C library's C locale
 * may reject bytes above 0x7f, so it is no oracle for them.
 *
 * The wide conversions are compared with the tree's narrow strtoll and
 * strtoull, which strtoll_test.c holds to the hosting library: a narrow
 * subject widened element by element must give the same value, errno and
 * end offset.  Elements above 0xff are checked separately, since a scan
 * that narrowed them would read 0x131 as the digit 1.
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int db_mblen(const char *, size_t);
int db_mbtowc(wchar_t *, const char *, size_t);
int db_wctomb(char *, wchar_t);
size_t db_mbstowcs(wchar_t *, const char *, size_t);
size_t db_wcstombs(char *, const wchar_t *, size_t);
intmax_t db_wcstoimax(const wchar_t *, wchar_t **, int);
uintmax_t db_wcstoumax(const wchar_t *, wchar_t **, int);
long long db_strtoll(const char *, char **, int);
unsigned long long db_strtoull(const char *, char **, int);

#define	GUARD	((wchar_t)0x5a5a)
#define	SUBJECT	40

static unsigned long checks;
static unsigned long failures;

static void
expect(int condition, const char *contract)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "wide test: %s\n", contract);
	}
}

static void
check_mblen(void)
{
	expect(db_mblen(NULL, 0) == 0, "mblen(NULL) reports no shift state");
	expect(db_mblen("", 1) == 0, "mblen of the null byte is 0");
	expect(db_mblen("a", 0) == -1, "mblen of no bytes is -1");
	expect(db_mblen("a", 1) == 1, "mblen of a byte is 1");
	expect(db_mblen("\xe9", 1) == 1, "mblen of a byte above 0x7f is 1");
}

static void
check_mbtowc(void)
{
	char byte[2];
	wchar_t wide;
	int value;

	expect(db_mbtowc(NULL, NULL, 0) == 0, "mbtowc(NULL) reports no state");
	wide = GUARD;
	expect(db_mbtowc(&wide, "a", 0) == -1 && wide == GUARD,
	    "mbtowc of no bytes is -1 and stores nothing");
	byte[1] = 'z';
	for (value = 0; value <= UCHAR_MAX; value++) {
		byte[0] = (char)value;
		wide = GUARD;
		expect(db_mbtowc(&wide, byte, 1) == (value != 0),
		    "mbtowc counts one byte, or 0 for the null byte");
		expect(wide == (wchar_t)value,
		    "mbtowc stores the byte read as unsigned char");
		expect(db_mbtowc(NULL, byte, 1) == (value != 0),
		    "mbtowc with a null pwc still counts the byte");
	}
}

static void
check_wctomb(void)
{
	static const wchar_t outside[] = { 0x100, 0x131, 0xffff, (wchar_t)-1 };
	char byte[2];
	wchar_t wide;
	size_t index;
	int value;

	expect(db_wctomb(NULL, L'a') == 0, "wctomb(NULL) reports no state");
	for (value = 0; value <= UCHAR_MAX; value++) {
		byte[0] = 'z';
		byte[1] = 'z';
		expect(db_wctomb(byte, (wchar_t)value) == 1,
		    "wctomb encodes 0 to UCHAR_MAX in one byte");
		expect((unsigned char)byte[0] == value && byte[1] == 'z',
		    "wctomb stores the byte and no other");
		wide = GUARD;
		(void)db_mbtowc(&wide, byte, 1);
		expect(wide == (wchar_t)value, "wctomb inverts mbtowc");
	}
	for (index = 0; index < sizeof(outside) / sizeof(outside[0]); index++) {
		byte[0] = 'z';
		expect(db_wctomb(byte, outside[index]) == -1 && byte[0] == 'z',
		    "wctomb rejects a value above UCHAR_MAX and stores nothing");
	}
}

static void
check_mbstowcs(void)
{
	wchar_t wide[6];
	size_t index;

	for (index = 0; index < 6; index++)
		wide[index] = GUARD;
	expect(db_mbstowcs(wide, "ab\xe9", 5) == 3,
	    "mbstowcs counts the characters before the null");
	expect(wide[0] == L'a' && wide[1] == L'b' && wide[2] == 0xe9 &&
	    wide[3] == 0 && wide[4] == GUARD,
	    "mbstowcs stores the characters and the null, then stops");

	for (index = 0; index < 6; index++)
		wide[index] = GUARD;
	expect(db_mbstowcs(wide, "abc", 2) == 2,
	    "mbstowcs stops at the limit");
	expect(wide[0] == L'a' && wide[1] == L'b' && wide[2] == GUARD,
	    "mbstowcs stores no null past the limit");

	wide[0] = GUARD;
	expect(db_mbstowcs(wide, "abc", 0) == 0 && wide[0] == GUARD,
	    "mbstowcs with a zero limit stores nothing");
	wide[1] = GUARD;
	expect(db_mbstowcs(wide, "", 2) == 0 && wide[0] == 0 &&
	    wide[1] == GUARD, "mbstowcs of the empty string stores the null");
	expect(db_mbstowcs(NULL, "ab\xe9", 0) == 3,
	    "mbstowcs with a null pwcs counts the whole string");
}

static void
check_wcstombs(void)
{
	static const wchar_t text[] = { L'a', L'b', 0xe9, 0 };
	static const wchar_t bad[] = { L'a', 0x100, L'c', 0 };
	char byte[6];

	memset(byte, 'z', sizeof(byte));
	expect(db_wcstombs(byte, text, 5) == 3,
	    "wcstombs counts the bytes before the null");
	expect(memcmp(byte, "ab\xe9\0z", 5) == 0,
	    "wcstombs stores the bytes and the null, then stops");

	memset(byte, 'z', sizeof(byte));
	expect(db_wcstombs(byte, text, 2) == 2 && byte[2] == 'z',
	    "wcstombs stops at the limit without a null");
	expect(db_wcstombs(byte, text, 0) == 0 && byte[0] == 'a',
	    "wcstombs with a zero limit stores nothing");

	memset(byte, 'z', sizeof(byte));
	expect(db_wcstombs(byte, bad, 5) == (size_t)-1,
	    "wcstombs rejects a value above UCHAR_MAX");
	memset(byte, 'z', sizeof(byte));
	expect(db_wcstombs(byte, bad, 1) == 1 && byte[0] == 'a' &&
	    byte[1] == 'z', "wcstombs reaches no value past the limit");
	expect(db_wcstombs(NULL, text, 0) == 3,
	    "wcstombs with a null s counts the whole conversion");
	expect(db_wcstombs(NULL, bad, 0) == (size_t)-1,
	    "wcstombs with a null s still rejects a value above UCHAR_MAX");
}

/*
 * One subject, widened, through both pairs of conversions.  The narrow
 * conversion is the oracle for value, errno and the end offset.
 */
static void
compare(const char *narrow, int base)
{
	wchar_t wide[SUBJECT + 1];
	char *narrow_end;
	wchar_t *wide_end;
	long long signed_expected;
	unsigned long long unsigned_expected;
	intmax_t signed_value;
	uintmax_t unsigned_value;
	int expected_errno;
	size_t length, index;

	length = strlen(narrow);
	for (index = 0; index <= length; index++)
		wide[index] = (wchar_t)(unsigned char)narrow[index];

	errno = 0;
	signed_expected = db_strtoll(narrow, &narrow_end, base);
	expected_errno = errno;
	errno = 0;
	signed_value = db_wcstoimax(wide, &wide_end, base);
	expect(signed_value == signed_expected, "wcstoimax value is strtoll's");
	expect(errno == expected_errno, "wcstoimax errno is strtoll's");
	expect(wide_end - wide == narrow_end - narrow,
	    "wcstoimax end offset is strtoll's");

	errno = 0;
	unsigned_expected = db_strtoull(narrow, &narrow_end, base);
	expected_errno = errno;
	errno = 0;
	unsigned_value = db_wcstoumax(wide, &wide_end, base);
	expect(unsigned_value == unsigned_expected,
	    "wcstoumax value is strtoull's");
	expect(errno == expected_errno, "wcstoumax errno is strtoull's");
	expect(wide_end - wide == narrow_end - narrow,
	    "wcstoumax end offset is strtoull's");
}

static void
check_fixed_subjects(void)
{
	static const char *const subjects[] = {
		"0", "-0", "+7", "  \t\n\v\f\r42", "9223372036854775807",
		"9223372036854775808", "-9223372036854775808",
		"-9223372036854775809", "18446744073709551615",
		"18446744073709551616", "-1", "0x", "0xg", "0x1F", "0X1f",
		"017", "08", "zz", "ZZ", "1z", "", " ", "-", "+-1",
		"99999999999999999999999999", "\3511", "1\351"
	};
	static const int bases[] = { 0, 2, 8, 10, 16, 36 };
	size_t subject, base;

	for (subject = 0; subject < sizeof(subjects) / sizeof(subjects[0]);
	    subject++)
		for (base = 0; base < sizeof(bases) / sizeof(bases[0]); base++)
			compare(subjects[subject], bases[base]);
	compare("12", 1);
	compare("12", 37);
}

/* A fixed linear congruential sequence keeps every run identical. */
static unsigned long
next_random(unsigned long *state)
{
	*state = *state * 1103515245UL + 12345UL;
	return (*state >> 16) & 0x7fffUL;
}

static void
check_generated_subjects(void)
{
	static const char alphabet[] = " \t+-0123456789abcfxzABFXZ";
	static const int bases[] = { 0, 2, 8, 10, 16, 36 };
	char narrow[SUBJECT + 1];
	unsigned long state = 1;
	size_t length, index;
	int round;

	for (round = 0; round < 20000; round++) {
		length = next_random(&state) % SUBJECT;
		for (index = 0; index < length; index++)
			narrow[index] = alphabet[next_random(&state) %
			    (sizeof(alphabet) - 1)];
		narrow[length] = '\0';
		compare(narrow, bases[next_random(&state) %
		    (sizeof(bases) / sizeof(bases[0]))]);
	}
}

/*
 * Elements above 0xff end the subject sequence.  Each low byte below is a
 * character the narrow grammar would accept.
 */
static void
check_wide_elements(void)
{
	static const wchar_t lead_digit[] = { 0x131, 0 };
	static const wchar_t mid_digit[] = { L'1', 0x132, 0 };
	static const wchar_t lead_space[] = { 0x120, L'5', 0 };
	static const wchar_t lead_sign[] = { 0x12d, L'5', 0 };
	static const wchar_t hex_mark[] = { L'0', 0x178, L'f', 0 };
	static const wchar_t full_width[] = { 0xff11, 0 };
	wchar_t *end;

	end = NULL;
	expect(db_wcstoimax(lead_digit, &end, 10) == 0 && end == lead_digit,
	    "wcstoimax reads 0x131 as no digit");
	expect(db_wcstoumax(mid_digit, &end, 10) == 1 && end == mid_digit + 1,
	    "wcstoumax stops at 0x132");
	expect(db_wcstoimax(lead_space, &end, 10) == 0 && end == lead_space,
	    "wcstoimax reads 0x120 as no white space");
	expect(db_wcstoimax(lead_sign, &end, 10) == 0 && end == lead_sign,
	    "wcstoimax reads 0x12d as no sign");
	expect(db_wcstoumax(hex_mark, &end, 16) == 0 && end == hex_mark + 1,
	    "wcstoumax reads 0x178 as no hex prefix");
	expect(db_wcstoimax(full_width, &end, 0) == 0 && end == full_width,
	    "wcstoimax reads a full-width digit as no digit");
	expect(db_wcstoimax(lead_digit, NULL, 10) == 0,
	    "wcstoimax accepts a null end pointer");
}

int
main(void)
{
	check_mblen();
	check_mbtowc();
	check_wctomb();
	check_mbstowcs();
	check_wcstombs();
	check_fixed_subjects();
	check_generated_subjects();
	check_wide_elements();
	printf("wide test: %lu checks, %lu failures\n", checks, failures);
	return failures != 0;
}
