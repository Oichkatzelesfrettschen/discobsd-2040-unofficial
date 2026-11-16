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
 * strtof() per C17 7.22.1.3 over the tree's strtod(): value, end pointer
 * and range errors.  Every in-range input is exactly representable as a
 * float, so the expected value does not depend on how strtod() rounds.
 * The underflow inputs are nonzero doubles that narrow to zero; an input
 * strtod() itself returns as zero carries no error, which 7.22.1.3p10
 * leaves to the implementation.
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float (*volatile convert)(const char *restrict, char **restrict) =
    strtof;
static unsigned checks;

struct exact {
	const char *text;
	float value;
	int consumed;
};

int
main(void)
{
	static const struct exact exact[] = {
		{ "0", 0.0f, 1 }, { "1", 1.0f, 1 }, { "-1", -1.0f, 2 },
		{ "1.5", 1.5f, 3 }, { "-2.25", -2.25f, 5 },
		{ "  12.5x", 12.5f, 6 }, { "1e3", 1000.0f, 3 },
		{ "65536", 65536.0f, 5 }, { "0.125", 0.125f, 5 },
		{ "16777216", 16777216.0f, 8 }, { "+4e0z", 4.0f, 4 },
		{ "x", 0.0f, 0 }
	};
	static const char *const over[] = { "1e39", "3.5e38", "1e400" };
	static const char *const under[] = { "1e-50", "1e-46" };
	unsigned i;
	char *end;
	float value;

	for (i = 0; i < sizeof(exact) / sizeof(exact[0]); i++) {
		errno = 0;
		value = convert(exact[i].text, &end);
		checks++;
		if (value != exact[i].value ||
		    end != exact[i].text + exact[i].consumed || errno != 0) {
			printf("FAIL strtof: \"%s\" gives %g, %d bytes, "
			    "errno %d\n", exact[i].text, (double)value,
			    (int)(end - exact[i].text), errno);
			return 1;
		}
	}
	for (i = 0; i < sizeof(over) / sizeof(over[0]); i++) {
		errno = 0;
		value = convert(over[i], &end);
		checks++;
		if (value != HUGE_VALF || errno != ERANGE || *end != '\0') {
			printf("FAIL strtof: overflow \"%s\" gives %g, "
			    "errno %d\n", over[i], (double)value, errno);
			return 1;
		}
		errno = 0;
		checks++;
		if (convert(over[i] - 0, NULL) != HUGE_VALF) {
			printf("FAIL strtof: null end pointer\n");
			return 1;
		}
	}
	errno = 0;
	value = convert("-1e39", &end);
	checks++;
	if (value != -HUGE_VALF || errno != ERANGE) {
		printf("FAIL strtof: negative overflow gives %g, errno %d\n",
		    (double)value, errno);
		return 1;
	}
	for (i = 0; i < sizeof(under) / sizeof(under[0]); i++) {
		errno = 0;
		value = convert(under[i], &end);
		checks++;
		if (value != 0.0f || errno != ERANGE) {
			printf("FAIL strtof: underflow \"%s\" gives %g, "
			    "errno %d\n", under[i], (double)value, errno);
			return 1;
		}
	}
	printf("c17 strtof: %u checks passed\n", checks);
	return 0;
}
