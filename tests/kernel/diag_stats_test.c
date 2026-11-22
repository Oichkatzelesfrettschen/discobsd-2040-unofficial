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
 * Host gate for sys/kern/subr_diag.c: counter arithmetic, the fault record
 * ring and the console message ring behind machdep.diag_stats and
 * machdep.diag_msgbuf.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/diag_stats.h>

/* The interrupt-level model the host machine/intr.h renames onto. */
int hk_ipl;
unsigned hk_ipl_raises;

static unsigned copyout_fail_after;	/* copyouts allowed; 0 disables */
static unsigned copyout_calls;
static unsigned copyout_print;		/* bytes printed inside the next copyout */
static unsigned put_count;		/* pattern characters printed so far */

static void put_pattern(unsigned n);

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	const char *src = from;
	char *dst = to;

	copyout_calls++;
	if (copyout_print != 0) {
		unsigned printed = copyout_print;

		copyout_print = 0;
		put_pattern(printed);
	}
	if (copyout_fail_after != 0 && copyout_calls > copyout_fail_after)
		return EFAULT;
	while (nbytes-- != 0)
		*dst++ = *src++;
	return 0;
}

static void
put_string(const char *s)
{
	while (*s != '\0')
		diag_msgputc((unsigned char)*s++);
}

static void
put_pattern(unsigned n)
{
	while (n-- != 0)
		diag_msgputc('a' + (int)(put_count++ % 26));
}

static void
check_counters(void)
{
	struct diag_stats stats;

	diag_add(DIAG_SD_READS, 3);
	diag_add(DIAG_SD_READS, 4);
	diag_max(DIAG_SD_SLOWEST_US, 90);
	diag_max(DIAG_SD_SLOWEST_US, 40);
	diag_set(DIAG_PSRAM_PROBE_ERR, 6);
	diag_set(DIAG_PSRAM_PROBE_ERR, 5);
	diag_add(DIAG_PRINT_US_TOTAL, UINT32_MAX - 1);
	diag_add(DIAG_PRINT_US_TOTAL, 5);
	diag_snapshot(&stats);
	HK_CHECK(stats.version == DIAG_STATS_VERSION);
	HK_CHECK(stats.size == sizeof stats);
	HK_CHECK(stats.value[DIAG_SD_READS] == 7);
	HK_CHECK(stats.value[DIAG_SD_SLOWEST_US] == 90);
	HK_CHECK(stats.value[DIAG_PSRAM_PROBE_ERR] == 5);
	HK_CHECK(stats.value[DIAG_PRINT_US_TOTAL] == UINT32_MAX);
	HK_CHECK(stats.value[DIAG_SD_WRITES] == 0);
}

static void
check_faults(void)
{
	struct diag_stats stats;
	struct diag_fault record;
	uint32_t i;

	for (i = 0; i < DIAG_NFAULT + 2; i++) {
		record.pid = i;
		record.pc = 0x1000 + i;
		record.lr = 0;
		record.psr = 0;
		record.exc_return = 0xfffffffdU;
		record.vectactive = 3;
		record.comm[0] = 'a' + i;
		record.comm[1] = '\0';
		diag_fault(&record);
	}
	diag_snapshot(&stats);
	HK_CHECK(stats.fault_total == DIAG_NFAULT + 2);
	/* The newest record sits at (total - 1) % NFAULT; the two oldest
	 * records are overwritten. */
	HK_CHECK(stats.fault[(stats.fault_total - 1) % DIAG_NFAULT].pid ==
	    DIAG_NFAULT + 1);
	HK_CHECK(stats.fault[(stats.fault_total - 1) % DIAG_NFAULT].pc ==
	    0x1000 + DIAG_NFAULT + 1);
	HK_CHECK(stats.fault[(stats.fault_total - 1) % DIAG_NFAULT].comm[0] ==
	    'a' + DIAG_NFAULT + 1);
	HK_CHECK(stats.fault[0].pid == DIAG_NFAULT);
	HK_CHECK(stats.fault[1].pid == DIAG_NFAULT + 1);
	HK_CHECK(stats.fault[2].pid == 2);
}

static void
check_message_ring(void)
{
	char out[2 * DIAG_MSGBUF_SIZE];
	size_t len, i;
	int error;

	/* An empty ring reads as an empty string. */
	len = sizeof out;
	error = diag_msgread(out, &len, NULL);
	HK_CHECK(error == 0 && len == 1 && out[0] == '\0');

	put_string("boot\nfault\n");
	len = 0;
	HK_CHECK(diag_msgread(NULL, &len, NULL) == 0);
	HK_CHECK(len == 12);
	len = sizeof out;
	error = diag_msgread(out, &len, NULL);
	HK_CHECK(error == 0 && len == 12 && hk_streq(out, "boot\nfault\n"));

	/* A short buffer reads the newest bytes that fit, NUL included. */
	len = 7;
	error = diag_msgread(out, &len, NULL);
	HK_CHECK(error == 0 && len == 7 && hk_streq(out, "fault\n"));
	len = 0;
	HK_CHECK(diag_msgread(out, &len, NULL) == ENOMEM && len == 12);
	len = sizeof out;
	HK_CHECK(diag_msgread(out, &len, out) == EPERM);

	/* A failed copy-out surfaces. */
	copyout_calls = 0;
	copyout_fail_after = 1;
	len = sizeof out;
	HK_CHECK(diag_msgread(out, &len, NULL) == EFAULT);
	copyout_fail_after = 0;

	/* Past one ring length the oldest bytes fall away, in order. */
	for (i = 0; i < DIAG_MSGBUF_SIZE + 100; i++)
		diag_msgputc('a' + (int)(i % 26));
	len = sizeof out;
	error = diag_msgread(out, &len, NULL);
	HK_CHECK(error == 0 && len == DIAG_MSGBUF_SIZE + 1);
	for (i = 0; i < DIAG_MSGBUF_SIZE && i < len - 1; i++) {
		size_t absolute = 100 + i;	/* index in the ring's tail run */

		HK_CHECK(out[i] == 'a' + (int)(absolute % 26));
	}
	HK_CHECK(out[DIAG_MSGBUF_SIZE] == '\0');
}

/*
 * A print that lands between two chunk copies moves the head. The ring
 * holds the pattern characters 0 to 1023; the first copy-out prints 100
 * more, which overwrite the slots of characters 0 to 99. The read keeps
 * its first chunk (characters 0 to 63), moves the next chunk's start to
 * character 100, the oldest byte still in the ring, and returns no byte
 * of the newer epoch.
 */
static void
check_print_during_read(void)
{
	char out[2 * DIAG_MSGBUF_SIZE], expected[2 * DIAG_MSGBUF_SIZE];
	size_t len, n = 0, c;

	/* One byte raises the interrupt level once, so an interrupt cannot split it. */
	{
		unsigned before = hk_ipl_raises;

		diag_msgputc('x');
		HK_CHECK(hk_ipl_raises == before + 1);
	}
	put_count = 0;
	put_pattern(DIAG_MSGBUF_SIZE);
	for (c = 0; c < 64; c++)
		expected[n++] = 'a' + (char)(c % 26);
	for (c = 100; c < DIAG_MSGBUF_SIZE; c++)
		expected[n++] = 'a' + (char)(c % 26);
	expected[n] = '\0';
	len = sizeof out;
	copyout_print = 100;
	HK_CHECK(diag_msgread(out, &len, NULL) == 0);
	HK_CHECK(len == n + 1);
	HK_CHECK(hk_streq(out, expected));
}

int
main(void)
{
	check_counters();
	check_faults();
	check_message_ring();
	check_print_during_read();
	return hk_verdict("diag_stats");
}
