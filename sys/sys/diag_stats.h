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


#ifndef _SYS_DIAG_STATS_H_
#define _SYS_DIAG_STATS_H_

#include <sys/types.h>
#include <sys/stdint.h>

/*
 * Counters, fault records and a console message ring for the testing
 * kernels, read through machdep.diag_stats and machdep.diag_msgbuf
 * (arch/rp2040/include/cpu.h). A kernel built without "options DIAG_STATS"
 * compiles every hook below to nothing and answers both nodes with
 * EOPNOTSUPP.
 *
 * Counters are 32-bit and saturate; the *_MAX and *_SLOWEST rows hold a
 * maximum, DIAG_PSRAM_PROBE_RAN and DIAG_PSRAM_PROBE_ERR hold the last value
 * set. A counter bumped from an interrupt and from process context takes
 * splhigh() around the update, so no count is lost.
 */
#define DIAG_STATS_VERSION	1
#define DIAG_NFAULT		4	/* fault records kept, newest wins */
#define DIAG_MSGBUF_SIZE	1024	/* power of two; sysctl(8) reads BUFSIZ */

/*
 * One row per metric: enumerator suffix and the name sysctl(8) prints.
 * New rows go before COUNT and bump DIAG_STATS_VERSION.
 */
#define DIAG_METRICS(X) \
	X(PSRAM_XFERS,		"psram_xfers") \
	X(PSRAM_BYTES,		"psram_bytes") \
	X(PSRAM_TIMEOUTS,	"psram_timeouts") \
	X(PSRAM_STALLS,		"psram_stalls") \
	X(PSRAM_RANGES,		"psram_ranges") \
	X(PSRAM_PROBE_RAN,	"psram_probe_ran") \
	X(PSRAM_PROBE_ERR,	"psram_probe_err") \
	X(SD_READS,		"sd_reads") \
	X(SD_WRITES,		"sd_writes") \
	X(SD_TIMEOUTS,		"sd_timeouts") \
	X(SD_IO_ERRORS,		"sd_io_errors") \
	X(SD_SLOWEST_US,	"sd_slowest_us") \
	X(UART_OVERRUNS,	"uart_overruns") \
	X(UART_BREAKS,		"uart_breaks") \
	X(UART_PARITY,		"uart_parity") \
	X(UART_FRAMING,		"uart_framing") \
	X(TTY_RAW_FLUSHES,	"tty_raw_flushes") \
	X(TTY_CBREAK_DROPS,	"tty_cbreak_drops") \
	X(TTY_COOKED_DROPS,	"tty_cooked_drops") \
	X(PRINTS,		"prints") \
	X(PRINT_US_TOTAL,	"print_us_total") \
	X(PRINT_US_MAX,		"print_us_max")

enum diag_metric {
#define DIAG_ENUM(name, text) DIAG_##name,
	DIAG_METRICS(DIAG_ENUM)
#undef DIAG_ENUM
	DIAG_METRIC_COUNT
};

/*
 * A fault arm_fault() reported. ARMv6-M omits the fault cause and address,
 * so the record holds what the exception frame and the process hold.
 * exc_return 0xfffffffd is a process fault; any other value is the kernel
 * faulting and ends in a panic.
 */
struct diag_fault {
	uint32_t pid;		/* (uint32_t)-1 when no process was current */
	uint32_t pc;
	uint32_t lr;
	uint32_t psr;
	uint32_t exc_return;
	uint32_t vectactive;	/* SCB ICSR VECTACTIVE: the exception number */
	char comm[8];
};

struct diag_stats {
	uint32_t version;
	uint32_t size;
	uint32_t fault_total;	/* faults since boot; record (n - 1) % NFAULT is newest */
	uint32_t value[DIAG_METRIC_COUNT];
	struct diag_fault fault[DIAG_NFAULT];
};

#ifdef KERNEL
#ifdef DIAG_STATS
void diag_add(enum diag_metric metric, uint32_t amount);
void diag_max(enum diag_metric metric, uint32_t value);
void diag_set(enum diag_metric metric, uint32_t value);
void diag_fault(const struct diag_fault *record);
void diag_msgputc(int c);
void diag_snapshot(struct diag_stats *stats);
int diag_msgread(void *oldp, size_t *oldlenp, void *newp);
u_int diag_us(void);		/* rp2040/clock.c: free-running microseconds */
#else
#define diag_add(metric, amount)	((void)0)
#define diag_max(metric, value)		((void)0)
#define diag_set(metric, value)		((void)0)
#define diag_fault(record)		((void)0)
#define diag_msgputc(c)			((void)0)
#endif
#endif

#endif /* _SYS_DIAG_STATS_H_ */
