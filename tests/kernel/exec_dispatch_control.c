/* Exercise production executable-format dispatch with recording dependencies. */
#include <stddef.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef char *caddr_t;
typedef long off_t;
#define _SYS_TYPES_H_
#define KERNEL
#include <sys/exec.h>
_Static_assert(sizeof(((struct exec_params *)0)->hdr) == SHSIZE, "exec header geometry");

#define DEBUG(...) ((void)0)
#define ENOEXEC 8
#define EACCES 13
#define EIO 5
#define IEXEC 0100
#define IREAD 0400
#define IFMT 0170000
#define IFREG 0100000
#define P_TRACED 1
#define UIO_READ 0
#define IO_UNIT 1

struct inode { unsigned i_mode; };
struct proc { unsigned p_flag; };
static struct inode inode;
static struct proc process;
static struct { struct proc *u_procp; } u = { &process };
static struct exec_params params;
static unsigned trace, calls, bad_arguments, handler_flags;
static int execute_denied, read_denied, read_error, residual, results[4], expected_length;
static char expected_byte;

static void record(unsigned event) { trace = trace * 8 + event; }

static int access(struct inode *node, int mode)
{
    if (node != &inode) bad_arguments = 1;
    if (mode == IEXEC) { record(1); return execute_denied; }
    if (mode == IREAD) { record(2); return read_denied; }
    bad_arguments = 1;
    return 1;
}

static int rdwri(int direction, struct inode *node, caddr_t buffer, size_t length,
                 off_t offset, int flags, int *remaining)
{
    record(3);
    if (direction != UIO_READ || node != &inode || buffer != (caddr_t)&params.hdr ||
        length != sizeof params.hdr || offset != 0 || flags != IO_UNIT ||
        params.hdr.sh[0] != '\0') bad_arguments = 1;
    *remaining = residual;
    if (residual < (int)length) buffer[0] = 'P';
    return read_error;
}

static int handler(struct exec_params *argument, unsigned index)
{
    record(index + 4);
    calls |= 1U << index;
    if (argument != &params || argument->ip != &inode ||
        argument->hdr_len != expected_length || argument->hdr.sh[0] != expected_byte ||
        argument->flags != handler_flags)
        bad_arguments = 1;
    handler_flags |= 1U << index;
    argument->flags = (u_char)handler_flags;
    return results[index];
}

int exec_hsaout_check(struct exec_params *argument) { return handler(argument, 0); }
int exec_aout_check(struct exec_params *argument) { return handler(argument, 1); }
int exec_elf_check(struct exec_params *argument) { return handler(argument, 2); }
int exec_script_check(struct exec_params *argument) { return handler(argument, 3); }

/* PRODUCTION_EXEC_TABLE */
/* PRODUCTION_EXEC_CHECK */

static void reset(void)
{
    inode.i_mode = IFREG | IEXEC;
    process.p_flag = 0;
    params.ip = &inode;
    params.hdr.sh[0] = 'X';
    params.hdr_len = -777;
    params.flags = 0;
    trace = calls = bad_arguments = handler_flags = 0;
    execute_denied = read_denied = read_error = 0;
    residual = (int)sizeof params.hdr - 3;
    expected_length = 3;
    expected_byte = 'P';
    for (unsigned index = 0; index < 4; index++) results[index] = ENOEXEC;
}

int main(void)
{
    unsigned first;
#ifdef NULL_TEST
    reset(); results[1] = 0;
    if (execsw[0].es_check != NULL || exec_check(&params) != 0 || calls != 2 ||
        trace != 11 * 8 + 5 || bad_arguments) return 33;
    return 0;
#endif
#ifdef EXEC_HSAOUT
    first = 0;
    if (nexecs != 4 || execsw[0].es_check != exec_hsaout_check ||
        execsw[1].es_check != exec_aout_check) return 30;
#else
    first = 1;
    if (nexecs != 3 || execsw[0].es_check != exec_aout_check) return 31;
#endif
    if (execsw[nexecs - 2].es_check != exec_elf_check ||
        execsw[nexecs - 1].es_check != exec_script_check) return 32;

    reset(); execute_denied = 1;
    if (exec_check(&params) != ENOEXEC || trace != 1 || bad_arguments) return 11;
    reset(); process.p_flag = P_TRACED; read_denied = 1;
    if (exec_check(&params) != ENOEXEC || trace != 10 || bad_arguments) return 12;
    reset(); inode.i_mode = IEXEC;
    if (exec_check(&params) != EACCES || trace != 1 || bad_arguments) return 13;
    reset(); inode.i_mode = IFREG;
    if (exec_check(&params) != EACCES || trace != 1 || bad_arguments) return 14;
    reset(); read_error = EIO;
    if (exec_check(&params) != EIO || trace != 11 || params.hdr_len != -777 ||
        calls || bad_arguments) return 15;

    for (unsigned selected = first; selected < 4; selected++) {
        unsigned expected_trace = 11;
        unsigned expected_calls = 0;
        reset(); results[selected] = 0;
        for (unsigned index = first; index <= selected; index++) {
            expected_trace = expected_trace * 8 + index + 4;
            expected_calls |= 1U << index;
        }
        if (exec_check(&params) != 0 || trace != expected_trace ||
            calls != expected_calls || bad_arguments) return 16 + (int)selected;
    }
    reset();
    if (exec_check(&params) != ENOEXEC || calls != (15U & ~(first ? 1U : 0U)) ||
        bad_arguments) return 20;
    reset(); results[first] = EIO;
    if (exec_check(&params) != EIO || calls != (1U << first) || bad_arguments) return 21;
    reset(); residual = (int)sizeof params.hdr; expected_length = 0; expected_byte = '\0';
    results[first] = 0;
    if (exec_check(&params) != 0 || calls != (1U << first) || bad_arguments) return 22;
    reset(); process.p_flag = P_TRACED; results[first] = 0;
    if (exec_check(&params) != 0 || trace != (10 * 8 + 3) * 8 + first + 4 ||
        calls != (1U << first) || bad_arguments) return 23;
    for (int shift = 3; shift <= 6; shift += 3) {
        reset(); inode.i_mode = IFREG | (IEXEC >> shift); results[first] = 0;
        if (exec_check(&params) != 0 || calls != (1U << first) || bad_arguments) return 24;
    }
    return 0;
}
