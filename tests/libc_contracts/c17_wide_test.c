/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The wide character members of lib/libc/wchar: C17 7.28, 7.29 and 7.30
 * over the one-byte encoding.
 *
 * The driver renames each tree member's symbols to tree_NAME in its
 * object, so the plain names here reach the hosting library.  String and
 * integer members are compared with the hosting library on generated
 * strings.  Conversions are held to stated values instead, because the
 * tree's encoding gives bytes 0x80 to 0xff the wide values of the same
 * number while a hosting "C" locale may reject them.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

wchar_t	*tree_wcscpy(wchar_t *restrict, const wchar_t *restrict);
wchar_t	*tree_wcsncpy(wchar_t *restrict, const wchar_t *restrict, size_t);
wchar_t	*tree_wmemcpy(wchar_t *restrict, const wchar_t *restrict, size_t);
wchar_t	*tree_wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t	*tree_wcscat(wchar_t *restrict, const wchar_t *restrict);
wchar_t	*tree_wcsncat(wchar_t *restrict, const wchar_t *restrict, size_t);
int	 tree_wcscmp(const wchar_t *, const wchar_t *);
int	 tree_wcscoll(const wchar_t *, const wchar_t *);
int	 tree_wcsncmp(const wchar_t *, const wchar_t *, size_t);
size_t	 tree_wcsxfrm(wchar_t *restrict, const wchar_t *restrict, size_t);
int	 tree_wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t	*tree_wcschr(const wchar_t *, wchar_t);
size_t	 tree_wcscspn(const wchar_t *, const wchar_t *);
wchar_t	*tree_wcspbrk(const wchar_t *, const wchar_t *);
wchar_t	*tree_wcsrchr(const wchar_t *, wchar_t);
size_t	 tree_wcsspn(const wchar_t *, const wchar_t *);
wchar_t	*tree_wcsstr(const wchar_t *, const wchar_t *);
wchar_t	*tree_wcstok(wchar_t *restrict, const wchar_t *restrict,
	    wchar_t **restrict);
wchar_t	*tree_wmemchr(const wchar_t *, wchar_t, size_t);
size_t	 tree_wcslen(const wchar_t *);
wchar_t	*tree_wmemset(wchar_t *, wchar_t, size_t);
wint_t	 tree_btowc(int);
int	 tree_wctob(wint_t);
int	 tree_mbsinit(const mbstate_t *);
size_t	 tree_mbrlen(const char *restrict, size_t, mbstate_t *restrict);
size_t	 tree_mbrtowc(wchar_t *restrict, const char *restrict, size_t,
	    mbstate_t *restrict);
size_t	 tree_wcrtomb(char *restrict, wchar_t, mbstate_t *restrict);
size_t	 tree_mbsrtowcs(wchar_t *restrict, const char **restrict, size_t,
	    mbstate_t *restrict);
size_t	 tree_wcsrtombs(char *restrict, const wchar_t **restrict, size_t,
	    mbstate_t *restrict);
double	 tree_wcstod(const wchar_t *restrict, wchar_t **restrict);
float	 tree_wcstof(const wchar_t *restrict, wchar_t **restrict);
long	 tree_wcstol(const wchar_t *restrict, wchar_t **restrict, int);
long long tree_wcstoll(const wchar_t *restrict, wchar_t **restrict, int);
unsigned long tree_wcstoul(const wchar_t *restrict, wchar_t **restrict, int);
unsigned long long tree_wcstoull(const wchar_t *restrict, wchar_t **restrict,
	    int);
int	 tree_iswctype(wint_t, wctype_t);
wctype_t tree_wctype(const char *);
wint_t	 tree_towlower(wint_t);
wint_t	 tree_towupper(wint_t);
wint_t	 tree_towctrans(wint_t, wctrans_t);
wctrans_t tree_wctrans(const char *);
size_t	 tree_mbrtoc16(char16_t *restrict, const char *restrict, size_t,
	    mbstate_t *restrict);
size_t	 tree_c16rtomb(char *restrict, char16_t, mbstate_t *restrict);
size_t	 tree_mbrtoc32(char32_t *restrict, const char *restrict, size_t,
	    mbstate_t *restrict);
size_t	 tree_c32rtomb(char *restrict, char32_t, mbstate_t *restrict);

#define CLASSES(F) F(alnum) F(alpha) F(blank) F(cntrl) F(digit) F(graph) \
	F(lower) F(print) F(punct) F(space) F(upper) F(xdigit)
#define DECLARE(n) int tree_isw##n(wint_t);
CLASSES(DECLARE)
#undef DECLARE

static unsigned long checks;
static uint32_t state = 0x2545f491U;

static uint32_t
next(void)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static int
sign(int value)
{
	return (value > 0) - (value < 0);
}

#define FAIL(name, status, detail) do {					\
	printf("FAIL %s: %s\n", name, detail);				\
	return status;							\
} while (0)
#define CHECK(name, status, condition) do {				\
	checks++;							\
	if (!(condition))						\
		FAIL(name, status, #condition);				\
} while (0)

#define LENGTH	12

/* A string over a small alphabet, so that matches and prefixes occur. */
static void
draw(wchar_t *s)
{
	static const wchar_t alphabet[] = { L'a', L'b', L'c', L' ', 0x100, 0xff };
	unsigned length = next() % LENGTH, i;

	for (i = 0; i < length; i++)
		s[i] = alphabet[next() % 6];
	s[length] = L'\0';
}

static int
strings(void)
{
	wchar_t a[LENGTH + 1], b[LENGTH + 1], tree[40], host[40];
	wchar_t *tree_state, *host_state, *t, *h;
	unsigned round, n;
	wchar_t c;

	for (round = 0; round < 20000; round++) {
		draw(a);
		draw(b);
		if (round % 3 == 0)	/* a common prefix */
			memcpy(b, a, (next() % LENGTH) * sizeof(wchar_t));
		n = next() % (LENGTH + 2);
		c = round % 7 == 0 ? L'\0' : a[next() % LENGTH % (wcslen(a) + 1)];

		CHECK("wcslen", 1, tree_wcslen(a) == wcslen(a));
		CHECK("wcscmp", 1, sign(tree_wcscmp(a, b)) == sign(wcscmp(a, b)));
		CHECK("wcscoll", 1,
		    sign(tree_wcscoll(a, b)) == sign(wcscmp(a, b)));
		CHECK("wcsncmp", 1,
		    sign(tree_wcsncmp(a, b, n)) == sign(wcsncmp(a, b, n)));
		CHECK("wmemcmp", 1, sign(tree_wmemcmp(a, b, n < LENGTH ? n :
		    LENGTH)) == sign(wmemcmp(a, b, n < LENGTH ? n : LENGTH)));
		CHECK("wcschr", 1, tree_wcschr(a, c) == wcschr(a, c));
		CHECK("wcsrchr", 1, tree_wcsrchr(a, c) == wcsrchr(a, c));
		CHECK("wmemchr", 1, tree_wmemchr(a, c, n < LENGTH ? n :
		    LENGTH) == wmemchr(a, c, n < LENGTH ? n : LENGTH));
		CHECK("wcsspn", 1, tree_wcsspn(a, b) == wcsspn(a, b));
		CHECK("wcscspn", 1, tree_wcscspn(a, b) == wcscspn(a, b));
		CHECK("wcspbrk", 1, tree_wcspbrk(a, b) == wcspbrk(a, b));
		b[next() % 3] = L'\0';	/* a short needle */
		CHECK("wcsstr", 1, tree_wcsstr(a, b) == wcsstr(a, b));

		/* Writers: the whole buffer must match, sentinels included. */
		wmemset(tree, L'#', 40);
		wmemset(host, L'#', 40);
		CHECK("wcscpy", 1, tree_wcscpy(tree, a) == tree);
		wcscpy(host, a);
		CHECK("wcscpy", 1, wmemcmp(tree, host, 40) == 0);
		CHECK("wcscat", 1, tree_wcscat(tree, b) == tree);
		wcscat(host, b);
		CHECK("wcscat", 1, wmemcmp(tree, host, 40) == 0);
		tree_wcsncat(tree, a, n);
		wcsncat(host, a, n);
		CHECK("wcsncat", 1, wmemcmp(tree, host, 40) == 0);
		wmemset(tree, L'#', 40);
		wmemset(host, L'#', 40);
		tree_wcsncpy(tree, a, n + 3);
		wcsncpy(host, a, n + 3);
		CHECK("wcsncpy", 1, wmemcmp(tree, host, 40) == 0);
		tree_wmemcpy(tree + 20, a, n < LENGTH ? n : LENGTH);
		wmemcpy(host + 20, a, n < LENGTH ? n : LENGTH);
		CHECK("wmemcpy", 1, wmemcmp(tree, host, 40) == 0);
		tree_wmemmove(tree + 2, tree, 16);	/* overlapping */
		wmemmove(host + 2, host, 16);
		CHECK("wmemmove", 1, wmemcmp(tree, host, 40) == 0);
		tree_wmemmove(tree, tree + 3, 16);
		wmemmove(host, host + 3, 16);
		CHECK("wmemmove", 1, wmemcmp(tree, host, 40) == 0);
		tree_wmemset(tree + 5, c, n);
		wmemset(host + 5, c, n);
		CHECK("wmemset", 1, wmemcmp(tree, host, 40) == 0);

		/* wcsxfrm: bounded identity, as strxfrm is. */
		wmemset(tree, L'#', 40);
		CHECK("wcsxfrm", 1, tree_wcsxfrm(tree, a, n) == wcslen(a));
		if (wcslen(a) < n)
			CHECK("wcsxfrm", 1, wcscmp(tree, a) == 0);
		else
			CHECK("wcsxfrm", 1, tree[0] == L'#' &&
			    tree[n > 0 ? n - 1 : 0] == L'#');
		CHECK("wcsxfrm", 1, tree[wcslen(a) + 1] == L'#');

		/* wcstok: the same tokens in the same places. */
		draw(a);
		wcscpy(tree, a);
		wcscpy(host, a);
		t = tree_wcstok(tree, L" c", &tree_state);
		h = wcstok(host, L" c", &host_state);
		while (t != NULL && h != NULL) {
			CHECK("wcstok", 1, t - tree == h - host &&
			    wcscmp(t, h) == 0);
			t = tree_wcstok(NULL, L" c", &tree_state);
			h = wcstok(NULL, L" c", &host_state);
		}
		CHECK("wcstok", 1, t == NULL && h == NULL);
		CHECK("wcstok", 1, wmemcmp(tree, host, wcslen(a) + 1) == 0);
	}
	return 0;
}

static int
conversions(void)
{
	static const char bytes[] = "a\x80\xffz";
	static const wchar_t wide[] = { L'a', 0x80, 0xff, L'z', L'\0' };
	static const wchar_t beyond[] = { L'a', 0x100, L'z', L'\0' };
	wchar_t out[8];
	char narrow[8];
	const char *source;
	const wchar_t *wsource;
	mbstate_t st;
	int c;

	memset(&st, 0, sizeof(st));
	for (c = 0; c <= UCHAR_MAX; c++) {
		CHECK("btowc", 2, tree_btowc(c) == (wint_t)c);
		CHECK("wctob", 2, tree_wctob((wint_t)c) == c);
	}
	CHECK("btowc", 2, tree_btowc(EOF) == WEOF);
	CHECK("wctob", 2, tree_wctob(0x100) == EOF && tree_wctob(WEOF) == EOF);
	CHECK("mbsinit", 2, tree_mbsinit(&st) != 0 && tree_mbsinit(NULL) != 0);

	out[0] = L'#';
	CHECK("mbrtowc", 2, tree_mbrtowc(out, "\xe9z", 2, &st) == 1 &&
	    out[0] == 0xe9);
	CHECK("mbrtowc", 2, tree_mbrtowc(out, "", 1, &st) == 0 &&
	    out[0] == L'\0');
	CHECK("mbrtowc", 2, tree_mbrtowc(out, "a", 0, &st) == (size_t)-2);
	CHECK("mbrtowc", 2, tree_mbrtowc(NULL, NULL, 0, &st) == 0);
	CHECK("mbrtowc", 2, tree_mbrtowc(NULL, "q", 1, &st) == 1);
	CHECK("mbrlen", 2, tree_mbrlen("\xff", 1, &st) == 1 &&
	    tree_mbrlen("", 1, &st) == 0 &&
	    tree_mbrlen("a", 0, &st) == (size_t)-2);

	narrow[0] = '#';
	CHECK("wcrtomb", 2, tree_wcrtomb(narrow, 0xff, &st) == 1 &&
	    (unsigned char)narrow[0] == 0xff);
	CHECK("wcrtomb", 2, tree_wcrtomb(NULL, 0x100, &st) == 1);
	errno = 0;
	CHECK("wcrtomb", 2, tree_wcrtomb(narrow, 0x100, &st) == (size_t)-1 &&
	    errno == EILSEQ);

	/* mbsrtowcs: complete, cut short, and counting only. */
	wmemset(out, L'#', 8);
	source = bytes;
	CHECK("mbsrtowcs", 2, tree_mbsrtowcs(out, &source, 8, &st) == 4 &&
	    source == NULL && wmemcmp(out, wide, 5) == 0 && out[5] == L'#');
	wmemset(out, L'#', 8);
	source = bytes;
	CHECK("mbsrtowcs", 2, tree_mbsrtowcs(out, &source, 2, &st) == 2 &&
	    source == bytes + 2 && out[1] == 0x80 && out[2] == L'#');
	source = bytes;
	CHECK("mbsrtowcs", 2, tree_mbsrtowcs(NULL, &source, 0, &st) == 4 &&
	    source == bytes);

	memset(narrow, '#', 8);
	wsource = wide;
	CHECK("wcsrtombs", 2, tree_wcsrtombs(narrow, &wsource, 8, &st) == 4 &&
	    wsource == NULL && memcmp(narrow, bytes, 5) == 0 &&
	    narrow[5] == '#');
	memset(narrow, '#', 8);
	wsource = wide;
	CHECK("wcsrtombs", 2, tree_wcsrtombs(narrow, &wsource, 3, &st) == 3 &&
	    wsource == wide + 3 && narrow[3] == '#');
	wsource = wide;
	CHECK("wcsrtombs", 2, tree_wcsrtombs(NULL, &wsource, 0, &st) == 4 &&
	    wsource == wide);
	errno = 0;
	wsource = beyond;
	CHECK("wcsrtombs", 2, tree_wcsrtombs(narrow, &wsource, 8, &st) ==
	    (size_t)-1 && errno == EILSEQ && wsource == beyond + 1);
	return 0;
}

static int
numbers(void)
{
	static const wchar_t *const text[] = {
		L"0", L"123", L"-123", L"+77", L"  42abc", L"0x7f", L"-0x80",
		L"0755", L"zz", L"", L"-", L"2147483647", L"2147483648",
		L"-2147483648", L"-2147483649", L"4294967295", L"4294967296",
		L"-1", L"9223372036854775807", L"9223372036854775808",
		L"-9223372036854775808", L"18446744073709551615",
		L"18446744073709551616", L"-18446744073709551615",
		L"99999999999999999999999", L"\t\n 5", L"1e5", L"7 8"
	};
	static const int bases[] = { 0, 10, 16, 8, 36, 2 };
	static const wchar_t *const real[] = {
		L"0", L"1.5", L"-2.25", L"  12.5x", L"1e3", L"0.125", L"x",
		L"+4e0z", L"65536", L".5", L"-"
	};
	unsigned i, j;
	wchar_t *tree_end, *host_end;
	int tree_errno, host_errno;

	for (i = 0; i < sizeof(text) / sizeof(text[0]); i++) {
		for (j = 0; j < sizeof(bases) / sizeof(bases[0]); j++) {
#define INTEGER(name, type) do {					\
	type tree_value, host_value;					\
	errno = 0;							\
	tree_value = tree_##name(text[i], &tree_end, bases[j]);		\
	tree_errno = errno;						\
	errno = 0;							\
	host_value = name(text[i], &host_end, bases[j]);		\
	host_errno = errno;						\
	CHECK(#name, 3, tree_value == host_value &&			\
	    tree_end == host_end &&					\
	    (tree_errno == ERANGE) == (host_errno == ERANGE));		\
} while (0)
			INTEGER(wcstol, long);
			INTEGER(wcstoul, unsigned long);
			INTEGER(wcstoll, long long);
			INTEGER(wcstoull, unsigned long long);
#undef INTEGER
		}
	}
	/*
	 * "0x" without a hexadecimal digit: C17 7.22.1.4 makes the subject
	 * sequence the zero alone.  Hosting libraries differ here, so the
	 * case is held to the standard, as check-c17-headers holds strtoll.
	 */
	{
		static const wchar_t prefix[] = L"0x";

		CHECK("wcstol", 3, tree_wcstol(prefix, &tree_end, 16) == 0 &&
		    tree_end == prefix + 1);
		CHECK("wcstoul", 3, tree_wcstoul(prefix, &tree_end, 0) == 0 &&
		    tree_end == prefix + 1);
	}
	for (i = 0; i < sizeof(real) / sizeof(real[0]); i++) {
		CHECK("wcstod", 3, tree_wcstod(real[i], &tree_end) ==
		    wcstod(real[i], &host_end) && tree_end == host_end);
		CHECK("wcstof", 3, tree_wcstof(real[i], &tree_end) ==
		    wcstof(real[i], &host_end) && tree_end == host_end);
	}
	CHECK("wcstod", 3, tree_wcstod(L"2.5", NULL) == 2.5);
	errno = 0;
	CHECK("wcstof", 3, tree_wcstof(L"1e39", NULL) > 3.4e38f &&
	    errno == ERANGE);
	return 0;
}

static int
classes(void)
{
	static const char *const names[] = {
		"alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower",
		"print", "punct", "space", "upper", "xdigit"
	};
	wint_t c;
	unsigned i;

	for (c = 0; c < 400; c++) {
		int basic = c < 128;

#define CLASS(n) CHECK("isw" #n, 4,					\
	(tree_isw##n(c) != 0) == (basic && is##n((int)c) != 0));
		CLASSES(CLASS)
#undef CLASS
		CHECK("towlower", 4, tree_towlower(c) ==
		    (basic ? (wint_t)tolower((int)c) : c));
		CHECK("towupper", 4, tree_towupper(c) ==
		    (basic ? (wint_t)toupper((int)c) : c));
		i = 0;
#define CLASS(n) CHECK("iswctype", 4,					\
	(tree_iswctype(c, tree_wctype(names[i])) != 0) ==		\
	(tree_isw##n(c) != 0)); i++;
		CLASSES(CLASS)
#undef CLASS
		CHECK("iswctype", 4, tree_iswctype(c, 0) == 0);
		CHECK("towctrans", 4, tree_towctrans(c,
		    tree_wctrans("tolower")) == tree_towlower(c));
		CHECK("towctrans", 4, tree_towctrans(c,
		    tree_wctrans("toupper")) == tree_towupper(c));
		CHECK("towctrans", 4, tree_towctrans(c, 0) == c);
	}
	CHECK("iswalpha", 4, tree_iswalpha(WEOF) == 0);
	CHECK("towlower", 4, tree_towlower(WEOF) == WEOF);
	for (i = 0; i < 12; i++)
		CHECK("wctype", 4, tree_wctype(names[i]) != 0);
	CHECK("wctype", 4, tree_wctype("bogus") == 0 &&
	    tree_wctype("") == 0 && tree_wctype("alph") == 0);
	CHECK("wctrans", 4, tree_wctrans("tolower") != 0 &&
	    tree_wctrans("toupper") != 0 &&
	    tree_wctrans("tolower") != tree_wctrans("toupper") &&
	    tree_wctrans("totitle") == 0);
	return 0;
}

static int
unicode(void)
{
	char16_t c16 = 0;
	char32_t c32 = 0;
	char byte = '#';
	mbstate_t st;

	memset(&st, 0, sizeof(st));
	CHECK("mbrtoc16", 5, tree_mbrtoc16(&c16, "\xe9", 1, &st) == 1 &&
	    c16 == 0xe9);
	CHECK("mbrtoc16", 5, tree_mbrtoc16(&c16, "", 1, &st) == 0 && c16 == 0);
	CHECK("mbrtoc16", 5, tree_mbrtoc16(&c16, "a", 0, &st) == (size_t)-2 &&
	    tree_mbrtoc16(NULL, NULL, 0, &st) == 0);
	CHECK("mbrtoc32", 5, tree_mbrtoc32(&c32, "\xff", 1, &st) == 1 &&
	    c32 == 0xff);
	CHECK("mbrtoc32", 5, tree_mbrtoc32(&c32, "", 1, &st) == 0 && c32 == 0);
	CHECK("mbrtoc32", 5, tree_mbrtoc32(&c32, "a", 0, &st) == (size_t)-2);
	CHECK("c16rtomb", 5, tree_c16rtomb(&byte, 0xff, &st) == 1 &&
	    (unsigned char)byte == 0xff);
	errno = 0;
	CHECK("c16rtomb", 5, tree_c16rtomb(&byte, 0x100, &st) == (size_t)-1 &&
	    errno == EILSEQ);
	errno = 0;
	CHECK("c16rtomb", 5, tree_c16rtomb(&byte, 0xd800, &st) == (size_t)-1 &&
	    errno == EILSEQ && tree_c16rtomb(NULL, 0x100, &st) == 1);
	CHECK("c32rtomb", 5, tree_c32rtomb(&byte, 0x41, &st) == 1 &&
	    byte == 'A');
	errno = 0;
	CHECK("c32rtomb", 5, tree_c32rtomb(&byte, 0x1f600, &st) ==
	    (size_t)-1 && errno == EILSEQ);
	return 0;
}

int
main(void)
{
	int status;

	if ((status = strings()) != 0 || (status = conversions()) != 0 ||
	    (status = numbers()) != 0 || (status = classes()) != 0 ||
	    (status = unicode()) != 0)
		return status;
	printf("c17 wide: %lu checks passed\n", checks);
	return 0;
}
