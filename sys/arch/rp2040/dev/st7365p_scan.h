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

#ifndef _ST7365P_SCAN_H
#define _ST7365P_SCAN_H

/*
 * The wrap rule of the GSCAN sampler (st7365p_scan()), kept apart from the
 * SPI loop so tests/picocalc/panel_scan_test.c runs it on the host.
 *
 * The controller's line counter advances by at most a few lines between
 * two reads, and a read that samples it while a carry moves through it
 * returns one wrong value: the board gave a single low line at 255 to 256
 * and at 127 to 128. A fall alone therefore does not mark a wrap.
 *
 * A sample is the scan's position when it lies from the last position to
 * SCAN_DROP above it. A fall of more than SCAN_DROP is a wrap candidate:
 * the next sample confirms it by continuing from the low line and rejects
 * it by lying anywhere else, and a rejecting sample is then judged against
 * the position before the candidate, so a misread that falls on a wrap
 * costs one sample of delay and not the wrap. A rise of more than SCAN_DROP is rejected
 * unless the next sample continues from it, which returns a track that
 * two equal misreads moved to the scan's position. The track begins only
 * when two samples in sequence agree, so a misread first sample is not
 * taken as the position. scan_step() returns 1 when it confirms
 * a wrap, whose time is that of the candidate sample (tr->wrap_us), and
 * sets tr->at when the sample it was given is the scan's position, so the
 * caller takes its line count from those samples alone.
 */
#define	SCAN_DROP		64	/* Lines no two samples lie apart. */

struct scan_track {
	unsigned int	prev;		/* The scan's position. */
	unsigned int	up;		/* The last rejected rise. */
	unsigned int	from;		/* The position before a candidate. */
	unsigned int	cand_us;	/* Time of the wrap candidate. */
	unsigned int	wrap_us;	/* Time of the last confirmed wrap. */
	unsigned int	rejected;	/* Samples off the position. */
	unsigned char	have, cand, rose;
	unsigned char	at;		/* The last sample was the position. */
};

static inline int
scan_step(struct scan_track *tr, unsigned int line, unsigned int us)
{
	tr->at = 0;
	if (tr->have < 2) {
		/* Two samples in sequence establish the position. */
		if (tr->have == 1 && line >= tr->up &&
		    line <= tr->up + SCAN_DROP) {
			tr->have = 2;
			tr->prev = line;
			tr->at = 1;
		} else {
			tr->rejected += tr->have;
			tr->have = 1;
			tr->up = line;
		}
		return 0;
	}
	if (tr->cand) {
		tr->cand = 0;
		if (line >= tr->prev && line <= tr->prev + SCAN_DROP) {
			tr->prev = line;
			tr->at = 1;
			tr->wrap_us = tr->cand_us;
			return 1;
		}
		/*
		 * The candidate was a misread. The position before it stands,
		 * and this sample is judged against that: it may itself be the
		 * fall of a wrap the misread fell on.
		 */
		tr->rejected++;
		tr->prev = tr->from;
	}
	if (line + SCAN_DROP < tr->prev) {
		tr->cand = 1;
		tr->cand_us = us;
		tr->rose = 0;
		tr->from = tr->prev;
		tr->prev = line;
		return 0;
	}
	if (line > tr->prev + SCAN_DROP) {
		if (tr->rose && line >= tr->up && line <= tr->up + SCAN_DROP) {
			tr->rose = 0;
			tr->prev = line;
			tr->at = 1;
		} else {
			tr->rejected++;
			tr->rose = 1;
			tr->up = line;
		}
		return 0;
	}
	tr->rose = 0;
	tr->prev = line;
	tr->at = 1;
	return 0;
}

#endif /* !_ST7365P_SCAN_H */
