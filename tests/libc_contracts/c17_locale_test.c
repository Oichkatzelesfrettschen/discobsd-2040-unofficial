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
 * The one-locale contract of C17 7.11 and 7.24.4: setlocale() recognizes
 * exactly "C", "POSIX" and "", localeconv() reports the "C" values of
 * 7.11.2.1p3, strcoll() orders as strcmp() does and strxfrm() is a bounded
 * identity.  Functions are reached through volatile pointers so that no
 * call is folded.
 */
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

static char *(*volatile select_locale)(int, const char *) = setlocale;
static struct lconv *(*volatile conventions)(void) = localeconv;
static int (*volatile collate)(const char *, const char *) = strcoll;
static size_t (*volatile transform)(char *restrict, const char *restrict,
    size_t) = strxfrm;
static unsigned checks;

static int
sign(int value)
{
	return (value > 0) - (value < 0);
}

static int
check_setlocale(void)
{
	static const int categories[] = {
		LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME
	};
	static const char *const accepted[] = { NULL, "", "C", "POSIX" };
	static const char *const refused[] = {
		"c", "posix", "C ", "en_US.UTF-8", "C.UTF-8", "POSIX1"
	};
	unsigned i, j;
	const char *name;

	for (i = 0; i < sizeof(categories) / sizeof(categories[0]); i++) {
		for (j = 0; j < sizeof(accepted) / sizeof(accepted[0]); j++) {
			name = select_locale(categories[i], accepted[j]);
			checks++;
			if (name == NULL || strcmp(name, "C") != 0) {
				printf("FAIL setlocale: category %d refuses "
				    "an accepted name\n", categories[i]);
				return 1;
			}
		}
		for (j = 0; j < sizeof(refused) / sizeof(refused[0]); j++) {
			checks++;
			if (select_locale(categories[i], refused[j]) != NULL) {
				printf("FAIL setlocale: accepts \"%s\"\n",
				    refused[j]);
				return 1;
			}
		}
	}
	checks++;
	if (select_locale(12345, "C") != NULL) {
		printf("FAIL setlocale: accepts an unknown category\n");
		return 1;
	}
	return 0;
}

static int
check_localeconv(void)
{
	const struct lconv *lc = conventions();
	const char *const empty[] = {
		lc->thousands_sep, lc->grouping, lc->int_curr_symbol,
		lc->currency_symbol, lc->mon_decimal_point,
		lc->mon_thousands_sep, lc->mon_grouping, lc->positive_sign,
		lc->negative_sign
	};
	const char unavailable[] = {
		lc->int_frac_digits, lc->frac_digits, lc->p_cs_precedes,
		lc->p_sep_by_space, lc->n_cs_precedes, lc->n_sep_by_space,
		lc->p_sign_posn, lc->n_sign_posn, lc->int_p_cs_precedes,
		lc->int_n_cs_precedes, lc->int_p_sep_by_space,
		lc->int_n_sep_by_space, lc->int_p_sign_posn,
		lc->int_n_sign_posn
	};
	unsigned i;

	checks++;
	if (strcmp(lc->decimal_point, ".") != 0) {
		printf("FAIL localeconv: decimal_point \"%s\"\n",
		    lc->decimal_point);
		return 2;
	}
	for (i = 0; i < sizeof(empty) / sizeof(empty[0]); i++) {
		checks++;
		if (empty[i][0] != '\0') {
			printf("FAIL localeconv: string %u is not empty\n", i);
			return 2;
		}
	}
	for (i = 0; i < sizeof(unavailable); i++) {
		checks++;
		if (unavailable[i] != CHAR_MAX) {
			printf("FAIL localeconv: char %u is not CHAR_MAX\n", i);
			return 2;
		}
	}
	return 0;
}

static int
check_strcoll(void)
{
	static const char *const words[] = {
		"", "a", "A", "ab", "abc", "abd", "b", "\x7f", "\x80", "\xff",
		"a\xff", "a\x01", "Z", "z", "0", " "
	};
	unsigned i, j;

	for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		for (j = 0; j < sizeof(words) / sizeof(words[0]); j++) {
			checks++;
			if (sign(collate(words[i], words[j])) !=
			    sign(strcmp(words[i], words[j]))) {
				printf("FAIL strcoll: pair %u, %u\n", i, j);
				return 3;
			}
		}
	}
	return 0;
}

static int
check_strxfrm(void)
{
	static const char source[] = "collate";	/* seven bytes */
	char buffer[16];
	size_t size, i;

	for (size = 0; size <= 12; size++) {
		memset(buffer, '#', sizeof(buffer));
		checks++;
		if (transform(buffer, source, size) != 7) {
			printf("FAIL strxfrm: length at size %u\n",
			    (unsigned)size);
			return 4;
		}
		checks++;
		if (size > 7 && memcmp(buffer, source, 8) != 0) {
			printf("FAIL strxfrm: no copy at size %u\n",
			    (unsigned)size);
			return 4;
		}
		/* Nothing past the permitted size, or past the terminator. */
		for (i = size > 7 ? 8 : size; i < sizeof(buffer); i++) {
			checks++;
			if (buffer[i] != '#') {
				printf("FAIL strxfrm: byte %u written at "
				    "size %u\n", (unsigned)i, (unsigned)size);
				return 4;
			}
		}
	}
	checks++;
	if (transform(NULL, source, 0) != 7) {
		printf("FAIL strxfrm: null destination at size 0\n");
		return 4;
	}
	return 0;
}

int
main(void)
{
	int status;

	if ((status = check_setlocale()) != 0 ||
	    (status = check_localeconv()) != 0 ||
	    (status = check_strcoll()) != 0 ||
	    (status = check_strxfrm()) != 0)
		return status;
	printf("c17 locale: %u checks passed\n", checks);
	return 0;
}
