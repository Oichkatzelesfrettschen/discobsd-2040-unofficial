/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * Testing-kernel counters and the console message ring behind
 * machdep.diag_stats and machdep.diag_msgbuf; sys/sys/diag_stats.h names
 * the contract. Built only with "options DIAG_STATS".
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/diag_stats.h>

#define DIAG_MSGBUF_MASK	(DIAG_MSGBUF_SIZE - 1)
_Static_assert((DIAG_MSGBUF_SIZE & DIAG_MSGBUF_MASK) == 0,
    "DIAG_MSGBUF_SIZE is a power of two");

static struct diag_stats diag;
static char diag_msg[DIAG_MSGBUF_SIZE];
static volatile u_int diag_msg_head;	/* bytes written, modulo 2^32 */
static volatile int diag_msg_full;	/* the ring has wrapped once */

void
diag_add(enum diag_metric metric, uint32_t amount)
{
	int s = splhigh();

	if (amount > UINT32_MAX - diag.value[metric])
		diag.value[metric] = UINT32_MAX;
	else
		diag.value[metric] += amount;
	splx(s);
}

void
diag_max(enum diag_metric metric, uint32_t value)
{
	int s = splhigh();

	if (value > diag.value[metric])
		diag.value[metric] = value;
	splx(s);
}

void
diag_set(enum diag_metric metric, uint32_t value)
{
	diag.value[metric] = value;
}

void
diag_fault(const struct diag_fault *record)
{
	int s = splhigh();

	diag.fault[diag.fault_total % DIAG_NFAULT] = *record;
	if (diag.fault_total != UINT32_MAX)
		diag.fault_total++;
	splx(s);
}

/*
 * Called for each console character from putchar(). The store and the
 * index step run under splhigh(), so a printf() from an interrupt cannot
 * rewind the index between its load and its store. PRIMASK does not mask a
 * HardFault; a HardFault print that lands inside this window races the
 * index, and the damage is bounded to the bytes of that one print.
 */
void
diag_msgputc(int c)
{
	int s = splhigh();
	u_int head = diag_msg_head;

	diag_msg[head & DIAG_MSGBUF_MASK] = (char)c;
	if ((head & DIAG_MSGBUF_MASK) == DIAG_MSGBUF_MASK)
		diag_msg_full = 1;
	diag_msg_head = head + 1;
	splx(s);
}

void
diag_snapshot(struct diag_stats *stats)
{
	int s = splhigh();

	*stats = diag;
	splx(s);
	stats->version = DIAG_STATS_VERSION;
	stats->size = sizeof *stats;
}

/*
 * The newest bytes of the ring as a NUL-terminated string, at most as many
 * as the caller's buffer holds less the NUL, so a short buffer reads the
 * tail. A size query (oldp null) answers the full length. Each chunk is
 * copied under splhigh() and its start is held within one ring length of
 * the head at that moment, so output printed during the read drops the
 * oldest bytes of this read and never returns a byte from a newer epoch
 * of its slot.
 */
int
diag_msgread(void *oldp, size_t *oldlenp, void *newp)
{
	char chunk[64];
	u_int end, pos, now;
	size_t n, written = 0, k, i;
	int s, error = 0;

	if (newp)
		return EPERM;
	s = splhigh();
	end = diag_msg_head;
	n = diag_msg_full ? DIAG_MSGBUF_SIZE : end;
	splx(s);
	if (oldp == NULL) {
		*oldlenp = n + 1;
		return 0;
	}
	if (*oldlenp == 0) {
		*oldlenp = n + 1;
		return ENOMEM;
	}
	if (n > *oldlenp - 1)
		n = *oldlenp - 1;
	pos = end - n;
	while (pos != end && error == 0) {
		s = splhigh();
		now = diag_msg_head;
		if (now - pos > DIAG_MSGBUF_SIZE)
			pos = now - DIAG_MSGBUF_SIZE;
		k = end - pos;
		if (k > DIAG_MSGBUF_SIZE || k == 0) {
			splx(s);
			break;
		}
		if (k > sizeof chunk)
			k = sizeof chunk;
		for (i = 0; i < k; i++)
			chunk[i] = diag_msg[(pos + i) & DIAG_MSGBUF_MASK];
		splx(s);
		error = copyout(chunk, (char *)oldp + written, k);
		pos += k;
		written += k;
	}
	if (error == 0) {
		chunk[0] = '\0';
		error = copyout(chunk, (char *)oldp + written, 1);
	}
	*oldlenp = written + 1;
	return error;
}
