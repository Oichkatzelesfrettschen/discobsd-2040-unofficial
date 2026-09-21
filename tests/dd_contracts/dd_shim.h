/*
 * bin/dd/dd.c compiles into the gate with open(2), read(2), lseek(2),
 * exit(3) and main() renamed onto the names below, so the gate owns an
 * input whose reads fail on demand and reads back a status instead of
 * losing the process to exit(). The renamed declarations arrive through the
 * program's own <unistd.h>, <fcntl.h> and <stdlib.h>, which the -D options
 * rewrite; the shim compiles without those options and declares the same
 * signatures here.
 */
#ifndef DD_SHIM_H
#define DD_SHIM_H

#include <setjmp.h>
#include <sys/types.h>

/*
 * The synthetic input is DD_BLOCKS blocks of DD_BLOCK bytes, block k filled
 * with 'A'+k, and block DD_BAD_BLOCK refused with EIO however often it is
 * read. read(2) leaves the file offset where it stood when it fails, which
 * is the behavior the copy loop has to step over to reach block
 * DD_BAD_BLOCK+1.
 */
#define DD_FAULT_INPUT  "dd-contract-fault-input"
#define DD_BLOCK        512
#define DD_BLOCKS       4
#define DD_BAD_BLOCK    1

/*
 * A copy that fails to advance past the refused block reads it again on
 * every pass and never reaches the end of the input, so the shim stops
 * serving at DD_READ_LIMIT and marks the trace. The limit stands well above
 * the DD_BLOCKS+1 reads a correct run issues, and the mark is what the
 * check names, because a gate that waits out an endless loop reports a job
 * timeout rather than a verdict.
 */
#define DD_READ_LIMIT   (DD_BLOCKS * 4)
#define DD_OVERRUN      'x'

#define DD_TRACE_MAX    512

/*
 * One token per event in the order the program issued it: r<block> a served
 * block, e<block> a refused block, z<block> the end of the input, s<delta>
 * an lseek by that many bytes from the current offset, x<block> the read
 * limit reached. The buffer is a shared mapping, so a scenario running in a
 * child leaves the sequence where the parent reads it.
 */
struct dd_trace {
    unsigned len;
    char text[DD_TRACE_MAX];
};

extern struct dd_trace *dd_trace;
extern jmp_buf dd_jump;
extern int dd_jump_armed;

int dd_open(const char *, int, ...);
ssize_t dd_read(int, void *, size_t);
off_t dd_lseek(int, off_t, int);
_Noreturn void dd_exit(int);
int dd_main(int, char **);

void dd_shim_init(void);
void dd_shim_reset(void);
int dd_overran(void);

#endif /* DD_SHIM_H */
