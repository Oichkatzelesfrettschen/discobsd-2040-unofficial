/*
 * bin/dd/dd.c holds three contracts this gate measures.
 *
 * Operand arithmetic: number() converts a size, a count or a block offset
 * and has to reject what leaves the range before the conversion wraps,
 * because C leaves a signed overflow undefined and wraps an unsigned one.
 * The gate runs at ILP32, where long and off_t are four bytes as they are
 * on the RP2040, so the operands below wrap here exactly as they wrap on
 * the board; a 64-bit run would reject them for being large instead and
 * measure nothing.
 *
 * Read errors: read(2) leaves the file offset where it stood when it fails,
 * so conv=noerror has to step the input past the block it could not read,
 * and the end-of-input test has to precede the error handler that rewrites
 * the byte count from the buffer.
 *
 * Seek and truncation are contracts over real files and belong to
 * filesystem_test.sh beside this file.
 */
#include "dd_shim.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* dd.c defines these the old way, so the gate states the interface. */
extern char *string;
extern char *operand;
long number(long);

#define DD_BIG 2147483647L

static int checks;
static int failures;

static void
ok(int condition, const char *what)
{
    checks++;
    if (condition) {
        printf("ok   %s\n", what);
        return;
    }
    failures++;
    printf("FAIL %s\n", what);
}

/*
 * number() rejects an operand by calling exit(), which the shim turns into
 * a longjmp back to here, so a rejection is a return value.
 */
static int
parse(const char *text, long *out)
{
    static char held[64];

    if (setjmp(dd_jump) != 0)
        return (0);
    snprintf(held, sizeof held, "%s", text);
    string = held;
    operand = held;
    dd_jump_armed = 1;
    *out = number(DD_BIG);
    dd_jump_armed = 0;
    return (1);
}

static void
arithmetic_checks(void)
{
    long v;
    int saved, sink;

    /* number() names the rejected operand on stderr, which the gate reads
       through its return value rather than through the stream. */
    saved = dup(2);
    sink = open("/dev/null", O_WRONLY);
    if (saved < 0 || sink < 0) {
        perror("dd_contracts: /dev/null");
        exit(2);
    }
    dup2(sink, 2);

    ok(parse("512", &v) && v == 512, "number accepts 512");
    ok(parse("2k", &v) && v == 2048, "number scales k by 1024");
    ok(parse("3b", &v) && v == 1536, "number scales b by 512");
    ok(parse("2x3", &v) && v == 6, "number multiplies across x");
    ok(parse("2147483646", &v) && v == 2147483646L,
        "number accepts the largest operand inside the bound");

    ok(!parse("2147483647", &v), "number rejects the bound itself");
    ok(!parse("4294967296", &v),
        "number rejects 2^32 rather than reading it as 0");
    ok(!parse("4294967297", &v),
        "number rejects 2^32+1 rather than reading it as 1");
    ok(!parse("8388608k", &v),
        "number rejects a k suffix whose product reaches 2^33");
    ok(!parse("4194304b", &v),
        "number rejects a b suffix whose product reaches 2^31");
    ok(!parse("65536x65536", &v),
        "number rejects an x product of 2^32");
    ok(!parse("1073741824x2", &v),
        "number rejects an x product of 2^31");

    dup2(saved, 2);
    close(saved);
    close(sink);
}

/*
 * main() ends in exit() and the program's globals carry no reset, so a
 * scenario runs in a child and the parent reads its status and the trace
 * the shim left in the shared mapping.
 */
static int
run_dd(char **argv)
{
    pid_t pid;
    int status, argc = 0;

    while (argv[argc] != NULL)
        argc++;
    dd_shim_reset();
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        perror("dd_contracts: fork");
        exit(2);
    }
    if (pid == 0) {
        int sink = open("/dev/null", O_WRONLY);

        if (sink >= 0)
            dup2(sink, 2);
        dd_main(argc, argv);
        _exit(99);
    }
    if (waitpid(pid, &status, 0) < 0) {
        perror("dd_contracts: waitpid");
        exit(2);
    }
    if (!WIFEXITED(status))
        return (-1);
    return (WEXITSTATUS(status));
}

static size_t
slurp(const char *path, char *buf, size_t max)
{
    ssize_t got;
    size_t total = 0;
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return (0);
    while (total < max && (got = read(fd, buf + total, max - total)) > 0)
        total += (size_t)got;
    close(fd);
    return (total);
}

static int
all_zero(const char *buf, size_t from, size_t to)
{
    size_t i;

    for (i = from; i < to; i++)
        if (buf[i] != '\0')
            return (0);
    return (1);
}

static void
read_error_checks(const char *out)
{
    static char image[8 * DD_BLOCK];
    char *plain[] = { (char *)"dd", (char *)"if=" DD_FAULT_INPUT,
        NULL, (char *)"bs=512", NULL };
    char *noerror[] = { (char *)"dd", (char *)"if=" DD_FAULT_INPUT,
        NULL, (char *)"bs=512", (char *)"conv=noerror", NULL };
    char *synced[] = { (char *)"dd", (char *)"if=" DD_FAULT_INPUT,
        NULL, (char *)"bs=512", (char *)"conv=noerror,sync", NULL };
    char of[256];
    size_t len;
    int status;

    snprintf(of, sizeof of, "of=%s", out);
    plain[2] = of;
    noerror[2] = of;
    synced[2] = of;

    /*
     * The whole sequence: block 0 served, block 1 refused, the input
     * stepped one block forward, blocks 2 and 3 served, end of input. A
     * copy that repeats the refused block or stops at it writes a
     * different one.
     */
    status = run_dd(noerror);
    len = slurp(out, image, sizeof image);
    ok(status == 0, "conv=noerror completes the copy with a zero status");
    ok(strcmp(dd_trace->text, "r0 e1 s512 r2 r3 z4 ") == 0,
        "conv=noerror steps the input one block past the failed read");
    ok(len == 3 * DD_BLOCK,
        "conv=noerror copies the three readable blocks");
    ok(len > 0 && image[0] == 'A', "conv=noerror copies the first block");
    ok(len > 2 * DD_BLOCK && image[DD_BLOCK] == 'C',
        "conv=noerror copies the block after the failed one");
    ok(len == 3 * DD_BLOCK && image[2 * DD_BLOCK] == 'D',
        "conv=noerror copies the last block");

    status = run_dd(plain);
    ok(status == 1, "a read error without conv=noerror exits 1");
    ok(strcmp(dd_trace->text, "r0 e1 ") == 0,
        "a read error without conv=noerror stops at the failed block");

    status = run_dd(synced);
    len = slurp(out, image, sizeof image);
    ok(status == 0, "conv=noerror,sync completes the copy");
    ok(len == 4 * DD_BLOCK,
        "conv=noerror,sync pads the failed block to ibs");
    ok(len == 4 * DD_BLOCK && all_zero(image, DD_BLOCK, 2 * DD_BLOCK),
        "conv=noerror,sync pads with zeros");
    ok(len == 4 * DD_BLOCK && image[2 * DD_BLOCK] == 'C',
        "conv=noerror,sync keeps the block after the failed one in place");
}

int
main(void)
{
    char out[] = "/tmp/dd-contract-out.XXXXXX";
    int fd;

    dd_shim_init();

    fd = mkstemp(out);
    if (fd < 0) {
        perror("dd_contracts: mkstemp");
        return (2);
    }
    close(fd);

    arithmetic_checks();
    read_error_checks(out);

    unlink(out);

    printf("dd_contracts: %d checks, %d failures\n", checks, failures);
    return (failures == 0 ? 0 : 1);
}
