/*
 * Contract gate for _doscan, the formatted input conversion behind scanf,
 * fscanf, sscanf and the curses scanw.
 *
 * The tree's scanner is compiled here against the tree's headers, so the
 * FILE layout and the getc/ungetc macros under test are the target's. The
 * conversion entry points are renamed on the command line, which keeps the
 * host's own scanf out of the link and makes every call below reach the
 * tree source.
 *
 * The cases fall into three groups: the directives C17 7.21.6.2 defines,
 * the boundaries where a width or end of input truncates a conversion, and
 * the malformed inputs that the V7 scanner this replaces mishandled. The
 * last group is the calibration: an unwidthed %d fed more digits than a
 * 64-byte staging buffer holds drove that scanner past its frame, and a %n
 * or a float directive returned a success verdict while storing nothing.
 */

#include <limits.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The tree's errno, kept apart from the host's thread-local one. */
int scanf_test_errno;

int db_sscanf(const char *, const char *, ...);

static int checks;
static int failures;

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

/* A digit run longer than any staging buffer the scanner could hold. */
#define LONG_DIGITS 512

int
main(void)
{
	char text[LONG_DIGITS + 8];
	char sbuf[64];
	char cbuf[8];
	long lv;
	unsigned long ulv;
	double dv;
	int iv, iv2, n, r;
	unsigned int uv;
	short sv;
	unsigned short usv;
	signed char scv;
	unsigned char ucv;
	void *pv;
	size_t i;

	/*
	 * Group 1: the integer directives, their bases and their
	 * length modifiers.
	 */
	iv = -1;
	r = db_sscanf("42", "%d", &iv);
	check(r == 1 && iv == 42, "%d does not convert a plain decimal");

	iv = -1;
	r = db_sscanf("  \t\n 42", "%d", &iv);
	check(r == 1 && iv == 42, "%d does not skip leading white space");

	iv = 0;
	r = db_sscanf("-42", "%d", &iv);
	check(r == 1 && iv == -42, "%d does not convert a negative decimal");

	iv = 0;
	r = db_sscanf("+42", "%d", &iv);
	check(r == 1 && iv == 42, "%d does not accept a leading plus");

	iv = 0;
	r = db_sscanf("017", "%o", &iv);
	check(r == 1 && iv == 017, "%o does not convert octal");

	uv = 0;
	r = db_sscanf("ff", "%x", &uv);
	check(r == 1 && uv == 0xff, "%x does not convert hexadecimal");

	/*
	 * C17 7.21.6.2p12 makes X identical to x. The V7 scanner folded
	 * every uppercase conversion to lowercase and forced the long
	 * modifier with it, so %X wrote a long through an int pointer.
	 */
	uv = 0;
	r = db_sscanf("ff", "%X", &uv);
	check(r == 1 && uv == 0xff, "%X is not identical to %x");

	iv = 0;
	r = db_sscanf("0x1f", "%i", &iv);
	check(r == 1 && iv == 31, "%i does not detect a hexadecimal prefix");

	iv = 0;
	r = db_sscanf("017", "%i", &iv);
	check(r == 1 && iv == 15, "%i does not detect an octal prefix");

	iv = 0;
	r = db_sscanf("19", "%i", &iv);
	check(r == 1 && iv == 19, "%i does not default to decimal");

	iv = -1;
	r = db_sscanf("0", "%i", &iv);
	check(r == 1 && iv == 0, "%i does not convert a lone zero");

	uv = 0;
	r = db_sscanf("4294967295", "%u", &uv);
	check(r == 1 && uv == 4294967295u,
	    "%u does not convert the largest unsigned value");

	lv = 0;
	r = db_sscanf("2147483647", "%ld", &lv);
	check(r == 1 && lv == 2147483647L, "%ld does not convert a long");

	lv = 0;
	r = db_sscanf("42", "%D", &lv);
	check(r == 1 && lv == 42, "%D does not retain the long decimal extension");

	ulv = 0;
	r = db_sscanf("377", "%O", &ulv);
	check(r == 1 && ulv == 255, "%O does not retain the long octal extension");

	sv = 0;
	r = db_sscanf("300", "%hd", &sv);
	check(r == 1 && sv == 300, "%hd does not convert through a short");

	pv = NULL;
	r = db_sscanf("1000", "%p", &pv);
	check(r == 1 && pv == (void *)0x1000,
	    "%p does not convert a hexadecimal pointer");

	dv = 0.0;
	r = db_sscanf("1e100", "%lf", &dv);
	check(r == 1 && dv > 1e99 && dv < 1e101,
	    "%lf does not use the target strtod exponent table");

	dv = 0.0;
	r = db_sscanf("1e999", "%lf", &dv);
	check(r == 1 && dv > DBL_MAX,
	    "%lf does not overflow an extreme positive exponent");

	dv = 1.0;
	r = db_sscanf("1e-999", "%lf", &dv);
	check(r == 1 && dv == 0.0,
	    "%lf does not underflow an extreme negative exponent");

	/*
	 * Group 2: strings, characters and scansets.
	 */
	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("  hello world", "%s", sbuf);
	check(r == 1 && strcmp(sbuf, "hello") == 0,
	    "%s does not stop at white space");

	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("hello", "%3s", sbuf);
	check(r == 1 && strcmp(sbuf, "hel") == 0,
	    "%s does not honor its field width");

	memset(cbuf, '@', sizeof cbuf);
	r = db_sscanf("abc", "%2c", cbuf);
	check(r == 1 && cbuf[0] == 'a' && cbuf[1] == 'b' && cbuf[2] == '@',
	    "%c writes a terminator or the wrong count");

	memset(cbuf, '@', sizeof cbuf);
	r = db_sscanf("a", "%2c", cbuf);
	check(r == 1 && cbuf[0] == 'a' && cbuf[1] == '@',
	    "%c rejects a nonempty field shortened by end of input");

	memset(cbuf, '@', sizeof cbuf);
	r = db_sscanf(" x", "%c", cbuf);
	check(r == 1 && cbuf[0] == ' ',
	    "%c skips leading white space, which it must not");

	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("abc123", "%[a-c]", sbuf);
	check(r == 1 && strcmp(sbuf, "abc") == 0,
	    "%[ does not honor a range");

	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("abc123", "%[^0-9]", sbuf);
	check(r == 1 && strcmp(sbuf, "abc") == 0,
	    "%[^ does not negate its scanset");

	/* C17 7.21.6.2p6: a ] first in the list is a member of it. */
	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("]]x", "%[]]", sbuf);
	check(r == 1 && strcmp(sbuf, "]]") == 0,
	    "%[]] does not treat a leading bracket as a member");

	/*
	 * A scanset is rebuilt per directive. The V7 scanner kept one
	 * file-scope table, so a second directive inherited the first.
	 */
	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf("aaabbb", "%[a]%[b]", sbuf, sbuf + 16);
	check(r == 2 && strcmp(sbuf, "aaa") == 0 &&
	    strcmp(sbuf + 16, "bbb") == 0,
	    "a second scanset inherits the first");

	/*
	 * Group 3: assignment suppression, %n and literal matching.
	 */
	iv = 0;
	r = db_sscanf("12 34", "%*d %d", &iv);
	check(r == 1 && iv == 34, "%*d is counted or does not consume");

	/* C17 7.21.6.2p12: %n assigns but is not itself a conversion. */
	iv = 0;
	n = -1;
	r = db_sscanf("42abc", "%d%n", &iv, &n);
	check(r == 1 && iv == 42 && n == 2,
	    "%n does not report the characters consumed");

	n = -1;
	r = db_sscanf("  42", "%d%n", &iv, &n);
	check(n == 4, "%n does not count skipped white space");

	iv = 0;
	iv2 = 0;
	r = db_sscanf("3;4", "%d;%d", &iv, &iv2);
	check(r == 2 && iv == 3 && iv2 == 4,
	    "a literal directive does not match");

	iv = 0;
	r = db_sscanf("3,4", "%d;%d", &iv, &iv2);
	check(r == 1 && iv == 3,
	    "a failed literal directive does not stop the scan");

	/*
	 * Group 4: the boundaries.
	 */
	iv = 0;
	r = db_sscanf("12345", "%2d", &iv);
	check(r == 1 && iv == 12, "%d does not honor its field width");

	/*
	 * A width of one admits only the sign, which leaves no digit, so
	 * the directive is a matching failure rather than a conversion.
	 */
	iv = -99;
	r = db_sscanf("-5", "%1d", &iv);
	check(r == 0 && iv == -99,
	    "a width that admits only a sign still converts");

	iv = -99;
	r = db_sscanf("", "%d", &iv);
	check(r == EOF, "an empty input does not return EOF");

	iv = -99;
	r = db_sscanf("abc", "%d", &iv);
	check(r == 0 && iv == -99,
	    "a non-numeric input does not report a matching failure");

	iv = 0;
	r = db_sscanf("7", "%d %d", &iv, &iv2);
	check(r == 1 && iv == 7,
	    "input ending after one conversion does not return that count");

	iv = -99;
	r = db_sscanf("-", "%d", &iv);
	check(r == 0 && iv == -99, "a lone sign converts");

	iv = 0;
	r = db_sscanf("2147483648", "%d", &iv);
	check(r == 1 && iv == INT_MAX,
	    "%d does not saturate a positive int overflow");

	iv = 0;
	r = db_sscanf("-2147483649", "%d", &iv);
	check(r == 1 && iv == INT_MIN,
	    "%d does not saturate a negative int overflow");

	sv = 0;
	r = db_sscanf("32768", "%hd", &sv);
	check(r == 1 && sv == SHRT_MAX,
	    "%hd does not saturate a positive short overflow");

	sv = 0;
	r = db_sscanf("-32769", "%hd", &sv);
	check(r == 1 && sv == SHRT_MIN,
	    "%hd does not saturate a negative short overflow");

	scv = 0;
	r = db_sscanf("128", "%hhd", &scv);
	check(r == 1 && scv == SCHAR_MAX,
	    "%hhd does not saturate a positive signed-char overflow");

	scv = 0;
	r = db_sscanf("-129", "%hhd", &scv);
	check(r == 1 && scv == SCHAR_MIN,
	    "%hhd does not saturate a negative signed-char overflow");

	lv = 0;
	r = db_sscanf("2147483648", "%ld", &lv);
	check(r == 1 && lv == LONG_MAX,
	    "%ld does not saturate a positive long overflow");

	lv = 0;
	r = db_sscanf("-2147483649", "%ld", &lv);
	check(r == 1 && lv == LONG_MIN,
	    "%ld does not saturate a negative long overflow");

	ucv = 0;
	r = db_sscanf("256", "%hhu", &ucv);
	check(r == 1 && ucv == UCHAR_MAX,
	    "%hhu does not saturate an unsigned-char overflow");

	usv = 0;
	r = db_sscanf("65536", "%hu", &usv);
	check(r == 1 && usv == USHRT_MAX,
	    "%hu does not saturate an unsigned-short overflow");

	ulv = 0;
	r = db_sscanf("4294967296", "%lu", &ulv);
	check(r == 1 && ulv == ULONG_MAX,
	    "%lu does not saturate an unsigned-long overflow");

	/*
	 * Group 5: the malformed inputs. This is the calibration for the
	 * defect the replacement removes: the V7 scanner staged digits in
	 * a 64-byte automatic buffer while an absent width defaulted to
	 * 30000, so the run below wrote past its frame. sysctl reaches
	 * that path from a shipped program with a caller-written
	 * argument, converting the value of the CTLTYPE_LONG kern.hostid
	 * through %ld.
	 */
	for (i = 0; i < LONG_DIGITS; i++)
		text[i] = '9';
	text[LONG_DIGITS] = '\0';

	lv = 0;
	r = db_sscanf(text, "%ld", &lv);
	check(r == 1 && lv == LONG_MAX,
	    "an over-long digit run does not saturate a signed destination");

	uv = 0;
	r = db_sscanf(text, "%u", &uv);
	check(r == 1 && uv == UINT_MAX,
	    "an over-long digit run does not saturate an unsigned "
	    "destination");

	/* The same run behind a sign, and with the scan continuing after. */
	text[0] = '-';
	lv = 0;
	n = -1;
	r = db_sscanf(text, "%ld%n", &lv, &n);
	check(r == 1 && lv == LONG_MIN && n == LONG_DIGITS,
	    "a negative over-long digit run does not saturate or consumes "
	    "the wrong extent");

	/* An over-long string run, bounded by its width rather than a buffer. */
	for (i = 0; i < LONG_DIGITS; i++)
		text[i] = 'a';
	text[LONG_DIGITS] = '\0';
	memset(sbuf, '@', sizeof sbuf);
	r = db_sscanf(text, "%16s", sbuf);
	check(r == 1 && strlen(sbuf) == 16,
	    "%s does not stop at its field width");

	/*
	 * 0x with no hexadecimal digit after it: only the leading zero is
	 * a subject sequence. A stream guarantees one pushback, so the x
	 * stays consumed and the following directive sees what follows it.
	 */
	iv = -99;
	r = db_sscanf("0xg", "%i", &iv);
	check(r == 1 && iv == 0, "0x with no digit does not convert zero");

	if (failures == 0) {
		(void)write(1, "scanf contracts: pass\n", 22);
		return 0;
	}
	(void)write(2, "scanf contracts: fail\n", 22);
	(void)checks;
	return 1;
}
