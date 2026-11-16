/* Exercise the production packed loader's preflight and commit transitions. */
#include <stddef.h>
#include <sys/exec_hsaout.h>

typedef char *caddr_t;
#define DEBUG(...) ((void)0)
#define ENOEXEC 8
#define ENOMEM 12
#define EFTYPE 79
#define EXEC_COMMITTED_FAILURE 2
#define NO_ADDR ((caddr_t)~0U)
#define SSIZE 2048U
#define NBPW 4U
#define USER_WINDOW_MAX (192U * 1024U)
#define USER_TOP(process) ((unsigned)(user_window + sizeof user_window))
#define __user_data_start user_window
#define roundup(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

struct inode { unsigned i_count; unsigned long i_size; };
struct proc { struct inode *p_tip; unsigned p_tsize; int p_pid; };
struct memsect { caddr_t vaddr; unsigned len; };
struct exec_params {
    union { struct exec aout; } hdr;
    int hdr_len;
    unsigned argc, envc, argbc, envbc, flags;
    struct inode *ip;
    struct memsect text, data, bss, heap, stack;
};
struct buf { unsigned unused; };
_Alignas(4) static char user_window[USER_WINDOW_MAX];
static struct inode inode;
static struct proc process;
static struct { struct proc *u_procp; } u = { &process };
static struct buf buffer;
static int header_bad, hold_error, save_error, establish_error, fail_load;
static int holds, unholds, gets, releases, loads, saves, establishes, clears, setups;
static int premature_write, reference_releases;

void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *output = destination;
    for (size_t index = 0; index < count; index++) output[index] = (unsigned char)value;
    return destination;
}
void __aeabi_memclr4(void *destination, size_t count)
{
    (void)memset(destination, 0, count);
}
void __aeabi_memcpy4(void *destination, const void *source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    for (size_t index = 0; index < count; index++) output[index] = input[index];
}

static int hsx_headers(struct inode *ip, const struct exec *header, struct hsx *packed)
{
    (void)ip; (void)header;
    packed->x_ctext = 3; packed->x_cdata = 2;
    packed->x_textcrc = 11; packed->x_datacrc = 22;
    return header_bad ? HSX_BADSIG : HSX_OK;
}
static int exec_text_hold(struct inode *ip)
{
    (void)ip; holds++; return hold_error;
}
static void exec_text_unhold(struct inode *ip) { (void)ip; unholds++; }
static struct buf *geteblk(void) { gets++; return &buffer; }
static void brelse(struct buf *bp) { if (bp == &buffer) releases++; }
static int hsx_load_buf(struct buf *bp, struct inode *ip, unsigned off, unsigned clen,
                        caddr_t dst, unsigned rlen, unsigned crc)
{
    (void)bp; (void)ip; (void)off; (void)clen; (void)rlen; (void)crc;
    loads++;
    if (loads <= 2 && dst != NULL) premature_write = 1;
    if (loads > 2 && dst == NULL) premature_write = 1;
    return loads == fail_load ? -1 : 0;
}
static int exec_save_args(struct exec_params *epp) { (void)epp; saves++; return save_error; }
static int exec_estab(struct exec_params *epp)
{
    (void)epp; establishes++;
    if (user_window[0] != 0x5a) premature_write = 1;
    if (!establish_error) user_window[0] = 0;
    return establish_error;
}
static void exec_text_release(struct proc *p, struct inode *ip)
{
    (void)p; (void)ip; reference_releases++;
}
static void exec_clear(struct exec_params *epp) { (void)epp; clears++; }
static void exec_setupstack(unsigned entry, struct exec_params *epp)
{
    (void)entry; (void)epp; setups++;
}
static int printf(const char *format, ...) { (void)format; return 0; }

/* PACKED_LOADER_FUNCTION */

static void reset(struct exec_params *params)
{
    *params = (struct exec_params){0};
    inode = (struct inode){1, HSX_HDRSIZE + 5};
    process = (struct proc){0};
    N_SETMAGIC(params->hdr.aout, OMAGIC, MID_ZERO, EX_HSPACK);
    params->hdr.aout.a_text = 4; params->hdr.aout.a_data = 3;
    params->hdr.aout.a_entry = (unsigned)user_window | 1U;
    params->hdr_len = sizeof(struct exec); params->ip = &inode;
    header_bad = hold_error = save_error = establish_error = fail_load = 0;
    holds = unholds = gets = releases = loads = saves = establishes = 0;
    clears = setups = premature_write = reference_releases = 0;
    user_window[0] = 0x5a;
}

int main(void)
{
    struct exec_params params;
    for (int scenario = 0; scenario < 9; scenario++) {
        reset(&params);
        if (scenario == 1) header_bad = 1;
        if (scenario == 2) hold_error = 35;
        if (scenario == 3) fail_load = 1;
        if (scenario == 4) fail_load = 2;
        if (scenario == 5) save_error = 36;
        if (scenario == 6) establish_error = 37;
        if (scenario == 7) fail_load = 3;
        if (scenario == 8) fail_load = 4;
        int result = exec_hsaout_check(&params);
        if (premature_write) return 10 + scenario;
        if (holds != (scenario != 1)) return 20 + scenario;
        if (scenario == 0) {
            if (result || loads != 4 || establishes != 1 || saves != 1 || releases != 1 ||
                unholds || reference_releases != 1 || inode.i_count != 2 ||
                process.p_tip != &inode || clears != 1 || setups != 1) return 30;
        } else if (scenario >= 7) {
            if (result != EFTYPE || establishes != 1 || releases != 1 || unholds ||
                reference_releases != 1 || process.p_tip != &inode || inode.i_count != 2 ||
                !(params.flags & EXEC_COMMITTED_FAILURE) || clears || setups) return 40 + scenario;
        } else {
            int expected = scenario == 2 ? 35 : scenario == 5 ? 36 :
                           scenario == 6 ? 37 : EFTYPE;
            if (result != expected || user_window[0] != 0x5a ||
                establishes != (scenario == 6) || process.p_tip != NULL ||
                reference_releases || clears || setups || params.flags) return 50 + scenario;
            if (scenario >= 3 && (unholds != 1 || gets != 1 || releases != 1))
                return 60 + scenario;
            if (scenario <= 2 && (unholds || gets || releases)) return 70 + scenario;
        }
    }
    return 0;
}
