/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host test of the GSCAN wrap rule (sys/arch/rp2040/dev/st7365p_scan.h).
 * A model scans FRAME_LINES lines at LINE_US each and is read every
 * READ_US, as st7365p_scan() reads the controller; the cases add the
 * misreads the PicoCalc board returned on 2026-11-18 (one low line at the
 * 255 to 256 and 127 to 128 carries), a high misread, and a first sample
 * that is itself a misread. Each case requires the wraps a clean scan
 * gives, at the same times.
 */
#include <stdio.h>
#include <string.h>

#include "st7365p_scan.h"

#define	FRAME_LINES	484
#define	LINE_US		36	/* 484 lines in 17424 us, 57.4 Hz. */
#define	READ_US		13
#define	RUN_US		160000

static int failures;

static void
check(const char *what, unsigned long got, unsigned long want)
{
	if (got != want) {
		printf("FAIL %s: got %lu, want %lu\n", what, got, want);
		failures++;
	}
}

enum { CLEAN, LOW_CARRY, HIGH_CARRY, FIRST_LOW };

struct result {
	unsigned wraps, first_us, last_us, rejected, max_line;
};

static void
run(int how, struct result *r)
{
	struct scan_track tr;
	unsigned us, line, last = 0, n = 0;

	memset(&tr, 0, sizeof(tr));
	memset(r, 0, sizeof(*r));
	/* Start mid-frame, as a sampler does. */
	for (us = 0; us < RUN_US; us += READ_US, n++) {
		unsigned true_line = ((us + 300 * LINE_US) / LINE_US) %
		    FRAME_LINES;

		line = true_line;
		/* The first read after a carry returns the misread. */
		if (how == LOW_CARRY && true_line != last &&
		    (true_line == 256 || true_line == 128))
			line = 0;
		if (how == HIGH_CARRY && true_line != last && true_line == 256)
			line = 511;
		if (how == FIRST_LOW && n == 0)
			line = 0;
		last = true_line;
		if (scan_step(&tr, line, us)) {
			if (r->wraps == 0)
				r->first_us = tr.wrap_us;
			r->last_us = tr.wrap_us;
			r->wraps++;
		}
		if (tr.at && line > r->max_line)
			r->max_line = line;
	}
	r->rejected = tr.rejected;
}

int
main(void)
{
	struct result clean, r;

	run(CLEAN, &clean);
	/* 160000 us from line 300: wraps at 184 lines, then every 484. */
	check("clean wraps", clean.wraps, 9);
	check("clean rejected", clean.rejected, 0);
	check("clean highest line", clean.max_line, FRAME_LINES - 1);
	/* Line 0 begins at 184 * 36 = 6624 us; the read at 6630 sees it. */
	check("clean first wrap", clean.first_us, 6630);
	check("clean span is eight frames within a read",
	    (clean.last_us - clean.first_us + READ_US) / (8 * FRAME_LINES *
	    LINE_US), 1);

	run(LOW_CARRY, &r);
	check("low misreads: wraps", r.wraps, clean.wraps);
	check("low misreads: first wrap", r.first_us, clean.first_us);
	check("low misreads: last wrap", r.last_us, clean.last_us);
	/* Lines 128 and 256 of nine frames, less those before the start. */
	check("low misreads: rejected", r.rejected, 18);

	run(HIGH_CARRY, &r);
	check("high misreads: wraps", r.wraps, clean.wraps);
	check("high misreads: last wrap", r.last_us, clean.last_us);
	check("high misreads: rejected", r.rejected, 9);
	check("high misreads: highest line", r.max_line, FRAME_LINES - 1);

	run(FIRST_LOW, &r);
	check("misread first sample: wraps", r.wraps, clean.wraps);
	check("misread first sample: first wrap", r.first_us, clean.first_us);
	check("misread first sample: rejected", r.rejected, 2);

	if (failures) {
		printf("panel_scan_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("panel_scan_test: ok\n");
	return 0;
}
