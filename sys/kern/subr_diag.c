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
static u_int diag_msg_head;		/* bytes ever written */

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
	diag.fault_total++;
	splx(s);
}

/*
 * Called for each console character from putchar(). The write runs inside
 * printf() from interrupt and HardFault context, so it runs without a
 * lock: two writers racing garble one byte, never the ring's indices.
 */
void
diag_msgputc(int c)
{
	diag_msg[diag_msg_head & DIAG_MSGBUF_MASK] = (char)c;
	diag_msg_head++;
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
 * as the caller's buffer holds, so a short buffer reads the tail. A size
 * query (oldp null) answers the full length.
 */
int
diag_msgread(void *oldp, size_t *oldlenp, void *newp)
{
	char chunk[64];
	u_int head = diag_msg_head;
	size_t n = head < DIAG_MSGBUF_SIZE ? head : DIAG_MSGBUF_SIZE;
	size_t off, k, i;
	u_int first;
	int error = 0;

	if (newp)
		return EPERM;
	if (oldp == NULL) {
		*oldlenp = n + 1;
		return 0;
	}
	if (*oldlenp == 0)
		return ENOMEM;
	if (n > *oldlenp - 1)
		n = *oldlenp - 1;
	first = head - n;
	for (off = 0; off < n && error == 0; off += k) {
		k = n - off < sizeof chunk ? n - off : sizeof chunk;
		for (i = 0; i < k; i++)
			chunk[i] = diag_msg[(first + off + i) & DIAG_MSGBUF_MASK];
		error = copyout(chunk, (char *)oldp + off, k);
	}
	if (error == 0) {
		chunk[0] = '\0';
		error = copyout(chunk, (char *)oldp + n, 1);
	}
	*oldlenp = n + 1;
	return error;
}
