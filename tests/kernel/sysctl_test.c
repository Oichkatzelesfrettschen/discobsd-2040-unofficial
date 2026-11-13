/*
 * Host gate for the sysctl(2) dispatcher, value helpers and CTL_VM branch
 * in sys/kern/kern_sysctl.c.
 *
 * The gate links the kernel source with host copy and authorization doubles.
 * Two properties decide the CTL_VM_SWAPMAP node,
 * and both are about extent rather than value. The bytes a caller receives
 * begin at the mapent array, which swapmap[0].m_map addresses, so the struct
 * map descriptor's own words -- m_map, m_limit and m_name, all kernel
 * addresses -- stay inside the kernel. And the copy stops at m_limit, the
 * last usable slot, so it reaches neither the allocator's terminator slot nor
 * any object placed after the array.
 *
 * The fixture makes both falsifiable: a poisoned guard follows the entries,
 * and every entry value is a small integer no address can take. A copy that
 * started at the descriptor, or ran past m_limit, puts a poison or pointer
 * word where the gate reads an entry.
 *
 * sysctl_nlist() decodes a symbol name from the components after CPU_NLIST,
 * CTL_NLIST_BYTES to a component, first byte lowest. The gate spells names
 * with its own encoder and requires the exact name, a terminator in the last
 * component with zero bytes after it, a refusal of any new value, and that
 * an unprivileged __sysctl() reaches the node without consulting suser().
 *
 * proc_uarea() finds a process's user area for fill_from_u(): in place when
 * resident, through the pool decoder when pooled, and otherwise through
 * swap_with_buf_result(), which the gate links from sys/kern/vm_swp.c so
 * that the ring fold is the kernel's own. Hooks run inside geteblk() and the
 * transfer, where the kernel sleeps, and change the process there.
 *
 * vm_sysctl() reaches its length through a pointer difference, which is an
 * int at the target's width, so the gate builds ILP32 beside prf_test and
 * sigauth_test.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/dk.h>
#include <sys/tty.h>
#include <sys/vm.h>		/* vmmeter.h and vmsystm.h, which self-include */
#include <sys/map.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/vmparam.h>
#include <sys/errno.h>

/*
 * Slots in the fixture map. SLOTS - 1 of them are the caller's, because
 * m_limit addresses the last usable slot and the difference stops there.
 */
#define SLOTS		8
#define PAYLOAD_ENTS	(SLOTS - 1)
#define PAYLOAD_LEN	(PAYLOAD_ENTS * (int)sizeof(struct mapent))

/* A word no mapent of this fixture holds and no host address takes. */
#define POISON		0xa5

/* vm_sysctl() is kern_sysctl.c's own, declared nowhere a caller can see. */
sysctlfn vm_sysctl;

/* A port's machdep.c defines this: one descriptor for the swap device. */
struct map swapmap[1];

/*
 * The entries the descriptor addresses, with a poisoned guard immediately
 * after them, so a copy that runs past m_limit lands in bytes the gate can
 * name.
 */
static struct {
	struct mapent	ent[SLOTS + 1];
	char		guard[64];
} fixture;

static char fixture_name[] = "swapmap";

/* The kernel globals kern_sysctl.c reads, at the widths its headers give. */
struct user u;
struct proc proc[NPROC];
struct proc *freeproc, *zombproc, *allproc, *qs;
struct file file[NFILE];
struct inode inode[NINODE];
struct mount mount[NMOUNT];
struct vmtotal total;
struct vmrate cnt;		/* the swap transfer counts vm_swp.c meters */
struct vmrate rate;		/* VM_STATS: the last second's events */
struct vmsum sum;		/* VM_STATS: events since boot */
struct forkstat forkstat;
struct nchstats nchstats;
long cp_time[CPUSTATES];
long dk_xfer[DK_NDRIVE];
long dk_bytes[DK_NDRIVE];
char *dk_name[DK_NDRIVE];
long tk_nin;
long tk_nout;
struct timeval boottime;
short avenrun[3];
size_t freemem;
size_t physmem;
long hostid;
char hostname[MAXHOSTNAMELEN];
int hostnamelen;
int securelevel;
int hz = 1000;
int usechz = 1000;
u_int nswap;
u_int swapstart;
dev_t swapdev;
char cpu_model[64];
char machine[] = "gate";
char machine_arch[] = "gate";
const char version[] = "gate";
const char ostype[] = "gate";
const char osversion[] = "gate";
const char osrelease[] = "gate";

/*
 * The fixture's swap device: a ring of GATE_SWAP_END blocks whose first
 * GATE_SWAP_FIRST are reserved, laid out as init_main.c lays out the RP2040
 * map, so logical block GATE_SWAP_END + n names device block
 * GATE_SWAP_FIRST + n. The gate links the real sys/kern/vm_swp.c, so the
 * fold a user-area read goes through is the kernel's own. A request that
 * runs past the device returns short, as the flash driver returns end of
 * partition.
 */
#define GATE_SWAP_FIRST	4
#define GATE_SWAP_END	16

union gate_block {
	struct user	u;
	char		bytes[DEV_BSIZE];
};

static union gate_block swapdisk[GATE_SWAP_END];
static int swap_error;
static unsigned swap_reads;

/*
 * Each hook runs where the kernel sleeps: inside geteblk() and inside the
 * device transfer. A hook that changes the process models a swap-in, an
 * exit or another image's write while the reader is asleep.
 */
static void (*during_geteblk)(void);
static void (*during_io)(void);

static void
gate_strategy(struct buf *bp)
{
	u_int blocks = bp->b_bcount / DEV_BSIZE;

	swap_reads++;
	if (during_io != NULL)
		during_io();
	bp->b_resid = 0;
	if (swap_error)
		bp->b_flags |= B_ERROR;
	else if ((u_int)bp->b_blkno + blocks > GATE_SWAP_END)
		bp->b_resid = bp->b_bcount;
	else
		bcopy(swapdisk[bp->b_blkno].bytes, bp->b_addr, bp->b_bcount);
	bp->b_flags |= B_DONE;
}

const struct bdevsw bdevsw[1] = { { .d_strategy = gate_strategy } };

/* The destination of every copyout, and the byte the gate fills it with. */
#define OUTBUF		512
static char outbuf[OUTBUF];
static char outfill;

/* Fail before copying so each requested extent and error path is observable. */
static struct copy_state {
	unsigned calls;
	unsigned fail_at;
	u_int extent;
} copyin_state, copyout_state;
static int allow_superuser = 1;
static unsigned superuser_calls;

static void
reset_calls(void)
{
	bzero(&copyin_state, sizeof(copyin_state));
	bzero(&copyout_state, sizeof(copyout_state));
	allow_superuser = 1;
	superuser_calls = 0;
}

static void
reset_out(char fill)
{
	unsigned i;

	outfill = fill;
	for (i = 0; i < OUTBUF; i++)
		outbuf[i] = fill;
}

/* True when the whole output buffer still holds the byte reset_out wrote. */
static int
out_untouched(void)
{
	unsigned i;

	for (i = 0; i < OUTBUF; i++)
		if (outbuf[i] != outfill)
			return (0);
	return (1);
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	copyout_state.calls++;
	copyout_state.extent = nbytes;
	if (copyout_state.calls == copyout_state.fail_at)
		return (EFAULT);
	bcopy(from, to, nbytes);
	return (0);
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	copyin_state.calls++;
	copyin_state.extent = nbytes;
	if (copyin_state.calls == copyin_state.fail_at)
		return (EFAULT);
	bcopy(from, to, nbytes);
	return (0);
}

int
suser(void)
{
	superuser_calls++;
	if (!allow_superuser)
		u.u_error = EPERM;
	return (allow_superuser);
}

/*
 * The kernel entry points kern_sysctl.c's other branches call. The CTL_VM
 * branch reaches none of them, so each one reaching the gate is a defect in
 * the gate rather than a case it covers.
 */
void
sleep(caddr_t chan, int pri)
{
	(void)chan;
	(void)pri;
	panic("sysctl_test: sleep");
}

void
wakeup(caddr_t chan)
{
	(void)chan;
	panic("sysctl_test: wakeup");
}

/*
 * One buffer, which a reader holds at most once at a time and returns aged.
 * Its contents start as poison, so a reader that uses a block it never read
 * finds bytes no user area holds.
 */
static struct buf gate_buf;
static union gate_block gate_buf_data;
static unsigned geteblk_calls, brelse_calls;

struct buf *
geteblk(void)
{
	HK_CHECK(geteblk_calls == brelse_calls);
	geteblk_calls++;
	bzero(&gate_buf, sizeof(gate_buf));
	for (unsigned i = 0; i < sizeof(gate_buf_data); i++)
		gate_buf_data.bytes[i] = POISON;
	gate_buf.b_addr = gate_buf_data.bytes;
	gate_buf.b_flags = B_BUSY;
	if (during_geteblk != NULL)
		during_geteblk();
	return (&gate_buf);
}

void
brelse(struct buf *bp)
{
	HK_CHECK(bp == &gate_buf);
	HK_CHECK(bp->b_flags & B_AGE);
	brelse_calls++;
}

/* gate_strategy() completes every request before it returns. */
void
biowait(struct buf *bp)
{
	(void)bp;
}

#ifdef SWAPRAM
/*
 * The SwapRAM pool, by slot: an image present in the pool and the user area
 * swapram_uarea_prefix() decodes for it. The decoder's contract is an exact
 * expansion of the requested prefix or failure.
 */
static int pool_present[NPROC];
static union gate_block pool_user[NPROC];
static int pool_error;

int
swapram_present(struct proc *p)
{
	return (pool_present[p - proc]);
}

int
swapram_uarea_prefix(struct proc *p, void *dst, unsigned int len)
{
	if (!pool_present[p - proc] || pool_error || len > DEV_BSIZE)
		return (-1);
	bcopy(pool_user[p - proc].bytes, dst, len);
	return (0);
}
#endif

/* CPU_NLIST in both ports' machine/cpu.h, which the gate does not include. */
#define GATE_CPU_NLIST	3

/* Symbols of lengths 3, 4 and 10, so names end in each component position. */
static const struct sysctl_symbol gate_symbols[] = {
	{ "_hz",	11 },
	{ "_abc",	33 },
	{ "_dk_ndrive",	22 },
	{ NULL,		0 },
};

/*
 * A port's cpu_sysctl() routes CPU_NLIST to sysctl_nlist(); the double does
 * the same and nothing else, so any other machdep request is a gate defect.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	(void)newlen;
	if (namelen >= 1 && name[0] == GATE_CPU_NLIST)
		return (sysctl_nlist(gate_symbols, name + 1, namelen - 1, oldp,
		    oldlenp, newp));
	panic("sysctl_test: cpu_sysctl");
	return (0);
}

/*
 * Fill the fixture: a descriptor addressing ent[0] with ent[SLOTS - 1] as the
 * last usable slot, entries carrying values small enough that no address can
 * be mistaken for one, and poison from m_limit to the end of the object.
 *
 * vm_sysctl() reads an entry's value nowhere, so the slots at and past
 * m_limit carry poison rather than the terminator a running allocator keeps
 * there. That is what turns an overrun into a byte the gate can name instead
 * of a zero indistinguishable from a legitimate one.
 */
static void
reset_map(void)
{
	char *beyond;
	unsigned i;

	for (i = 0; i < PAYLOAD_ENTS; i++) {
		/* Free runs, ascending and non-adjacent, as subr_rmap.c keeps them. */
		fixture.ent[i].m_size = (i + 1) * 2;
		fixture.ent[i].m_addr = (i + 1) * 64;
	}
	beyond = (char *)&fixture.ent[PAYLOAD_ENTS];
	for (i = 0; i < sizeof(fixture) - PAYLOAD_LEN; i++)
		beyond[i] = POISON;

	swapmap[0].m_map = fixture.ent;
	swapmap[0].m_limit = &fixture.ent[SLOTS - 1];
	swapmap[0].m_name = fixture_name;
}

static int
call_vm(int id, void *oldp, size_t *oldlenp, void *newp)
{
	int name[1];

	name[0] = id;
	return (vm_sysctl(name, 1, oldp, oldlenp, newp, 0));
}

/*
 * A size query names the payload and nothing else: the entries below
 * m_limit, with the descriptor and the terminator slot outside.
 */
static void
test_swapmap_length(void)
{
	size_t len;
	int error;

	reset_map();
	len = 0;
	error = call_vm(VM_SWAPMAP, NULL, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);
	HK_CHECK(len < sizeof(fixture.ent));
	HK_CHECK(len != sizeof(struct map));
}

/*
 * The copied bytes are the entries. Reading them back as mapents recovers
 * the fixture's values, which the descriptor's pointer words cannot equal.
 */
static void
test_swapmap_payload(void)
{
	struct mapent got[PAYLOAD_ENTS];
	size_t len;
	int error;
	unsigned i;

	reset_map();
	reset_out(0);
	len = sizeof(outbuf);
	error = call_vm(VM_SWAPMAP, outbuf, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);

	bcopy(outbuf, got, sizeof(got));
	for (i = 0; i < PAYLOAD_ENTS; i++) {
		HK_CHECK(got[i].m_size == fixture.ent[i].m_size);
		HK_CHECK(got[i].m_addr == fixture.ent[i].m_addr);
	}
}

/*
 * No descriptor word and no guard byte crosses to the caller. The first
 * check names the disclosure directly; the second names the overread that
 * follows a copy of the payload's length taken from the descriptor's
 * address.
 */
static void
test_swapmap_encloses_nothing_else(void)
{
	char descbytes[sizeof(struct map)];
	size_t len;
	int error;
	unsigned i, j, match;

	reset_map();
	reset_out(0);
	len = sizeof(outbuf);
	error = call_vm(VM_SWAPMAP, outbuf, &len, NULL);
	HK_CHECK(error == 0);

	bcopy(swapmap, descbytes, sizeof(descbytes));
	match = 0;
	for (i = 0; i + sizeof(descbytes) <= len; i++) {
		for (j = 0; j < sizeof(descbytes); j++)
			if (outbuf[i + j] != descbytes[j])
				break;
		if (j == sizeof(descbytes))
			match++;
	}
	HK_CHECK(match == 0);

	for (i = 0; i < len; i++)
		HK_CHECK((unsigned char)outbuf[i] != POISON);

	/* Everything past the stated length is the gate's own fill. */
	for (i = len; i < OUTBUF; i++)
		HK_CHECK(outbuf[i] == 0);
}

/*
 * A buffer shorter than the payload takes the prefix that fits and learns
 * what the whole value needs. The node itself succeeds: truncation is
 * __sysctl()'s verdict, decided from the length reported here against the
 * buffer the caller offered, which test_syscall_enomem() covers.
 */
static void
test_swapmap_short_buffer(void)
{
	struct mapent got[PAYLOAD_ENTS];
	size_t len, offered;
	int error;
	unsigned i;

	reset_map();
	reset_out(POISON);
	offered = PAYLOAD_LEN - sizeof(struct mapent);
	len = offered;
	error = call_vm(VM_SWAPMAP, outbuf, &len, NULL);
	HK_CHECK(error == 0);

	/* The length is what the value needs, not what the buffer took. */
	HK_CHECK(len == (size_t)PAYLOAD_LEN);
	HK_CHECK(len > offered);

	/* The prefix is the leading entries, whole and in order. */
	bcopy(outbuf, got, offered);
	for (i = 0; i < offered / sizeof(struct mapent); i++) {
		HK_CHECK(got[i].m_size == fixture.ent[i].m_size);
		HK_CHECK(got[i].m_addr == fixture.ent[i].m_addr);
	}

	/* Nothing past the buffer the caller offered was written. */
	for (i = offered; i < OUTBUF; i++)
		HK_CHECK((unsigned char)outbuf[i] == POISON);
}

/*
 * A buffer of no length at all is the boundary of the same rule: nothing is
 * copied, and the length still names what the value needs, so a caller that
 * passes a zero length learns the size exactly as a null oldp does.
 */
static void
test_swapmap_zero_length_buffer(void)
{
	size_t len;
	int error;

	reset_map();
	reset_out(POISON);
	len = 0;
	error = call_vm(VM_SWAPMAP, outbuf, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);
	HK_CHECK(out_untouched());
}

/* The node is read-only, so a write attempt fails before any copy. */
static void
test_swapmap_readonly(void)
{
	size_t len;
	int newval;
	int error;

	reset_map();
	reset_out(POISON);
	newval = 0;
	len = sizeof(outbuf);
	error = call_vm(VM_SWAPMAP, outbuf, &len, &newval);
	HK_CHECK(error == EPERM);
	HK_CHECK(out_untouched());
}

/*
 * The query and the copy derive one length, so a caller that sizes a buffer
 * from the query and then reads gets exactly that many bytes.
 */
static void
test_swapmap_query_matches_copy(void)
{
	size_t queried, copied;
	int error;

	reset_map();
	queried = 0;
	error = call_vm(VM_SWAPMAP, NULL, &queried, NULL);
	HK_CHECK(error == 0);

	reset_out(0);
	copied = queried;
	error = call_vm(VM_SWAPMAP, outbuf, &copied, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(copied == queried);
}

/* The length follows the map, so a map with one usable slot yields one. */
static void
test_swapmap_tracks_limit(void)
{
	size_t len;
	int error;

	reset_map();
	swapmap[0].m_limit = &fixture.ent[1];
	len = 0;
	error = call_vm(VM_SWAPMAP, NULL, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == sizeof(struct mapent));

	reset_map();
	swapmap[0].m_limit = fixture.ent;
	len = 0;
	error = call_vm(VM_SWAPMAP, NULL, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == 0);
}

/* nswap is the swap device's size in DEV_BSIZE blocks, read-only. */
static void
test_nswap(void)
{
	size_t len;
	int got;
	int error;

	nswap = 768;
	len = sizeof(got);
	got = 0;
	error = call_vm(VM_NSWAP, &got, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == sizeof(int));
	HK_CHECK(got == 768);

	got = 0;
	len = sizeof(got);
	error = call_vm(VM_NSWAP, &got, &len, &got);
	HK_CHECK(error == EPERM);
}

static void
test_loadavg_and_meter(void)
{
	struct loadavg lav;
	struct vmtotal vmt;
	size_t len;
	int error;

	avenrun[0] = 11;
	avenrun[1] = 22;
	avenrun[2] = 33;
	len = sizeof(lav);
	error = call_vm(VM_LOADAVG, &lav, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == sizeof(lav));
	HK_CHECK(lav.fscale == 256);
	HK_CHECK(lav.ldavg[0] == 11 && lav.ldavg[2] == 33);

	total.t_rq = 4;
	len = sizeof(vmt);
	error = call_vm(VM_METER, &vmt, &len, NULL);
	HK_CHECK(error == 0);
	HK_CHECK(len == sizeof(vmt));
	HK_CHECK(vmt.t_rq == 4);
}

/*
 * Every name at this level is terminal, and every id the header leaves
 * unnamed is refused. Id 4 is the retired coremap node, which CTL_VM_NAMES
 * still carries a slot for.
 */
static void
test_name_space(void)
{
	size_t len;
	int name[2];
	int error;

	reset_map();
	len = sizeof(outbuf);
	name[0] = VM_SWAPMAP;
	name[1] = 0;
	error = vm_sysctl(name, 2, outbuf, &len, NULL, 0);
	HK_CHECK(error == ENOTDIR);

	len = sizeof(outbuf);
	error = call_vm(4, outbuf, &len, NULL);
	HK_CHECK(error == EOPNOTSUPP);

	len = sizeof(outbuf);
	error = call_vm(VM_MAXID, outbuf, &len, NULL);
	HK_CHECK(error == EOPNOTSUPP);

	len = sizeof(outbuf);
	error = call_vm(0, outbuf, &len, NULL);
	HK_CHECK(error == EOPNOTSUPP);
}

/*
 * The eight helpers, each against the same three questions: what a size
 * query reports, what a short buffer takes and reports, and what an exact
 * buffer delivers. sysctl(3) gives one answer for all of them, and before
 * this contract the four that assign *oldlenp inside "if (oldp)" -- string
 * and struct, in their writable forms -- left a size query's length
 * untouched, so a caller sizing a buffer from it allocated whatever it had
 * passed in.
 */
#define HELPER_SHORT	1	/* one byte, shorter than any value here */

static void
test_helper_lengths(void)
{
	static char str[32] = "hostname";
	static struct mapent st;
	size_t len;
	int ival;
	long lval;
	char small[8];

	/* A size query reports the value's length through every helper. */
	ival = 0;
	len = 0;
	HK_CHECK(sysctl_int(NULL, &len, NULL, 0, &ival) == 0);
	HK_CHECK(len == sizeof(int));

	len = 0;
	HK_CHECK(sysctl_rdint(NULL, &len, NULL, 7) == 0);
	HK_CHECK(len == sizeof(int));

	lval = 0;
	len = 0;
	HK_CHECK(sysctl_long(NULL, &len, NULL, 0, &lval) == 0);
	HK_CHECK(len == sizeof(long));

	len = 0;
	HK_CHECK(sysctl_rdlong(NULL, &len, NULL, 7) == 0);
	HK_CHECK(len == sizeof(long));

	len = 0;
	HK_CHECK(sysctl_string(NULL, &len, NULL, 0, str, sizeof(str)) == 0);
	HK_CHECK(len == 9);		/* "hostname" and its terminator */

	len = 0;
	HK_CHECK(sysctl_rdstring(NULL, &len, NULL, "hostname") == 0);
	HK_CHECK(len == 9);

	len = 0;
	HK_CHECK(sysctl_struct(NULL, &len, NULL, 0, &st, sizeof(st)) == 0);
	HK_CHECK(len == sizeof(st));

	len = 0;
	HK_CHECK(sysctl_rdstruct(NULL, &len, NULL, &st, sizeof(st)) == 0);
	HK_CHECK(len == sizeof(st));

	/*
	 * A one-byte buffer takes one byte and reports the whole length.
	 * The helper succeeds; __sysctl() is where that becomes ENOMEM.
	 */
	reset_out(POISON);
	ival = 0x41424344;
	len = HELPER_SHORT;
	HK_CHECK(sysctl_int(outbuf, &len, NULL, 0, &ival) == 0);
	HK_CHECK(len == sizeof(int));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	reset_out(POISON);
	len = HELPER_SHORT;
	HK_CHECK(sysctl_rdint(outbuf, &len, NULL, ival) == 0);
	HK_CHECK(len == sizeof(int));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	reset_out(POISON);
	lval = 1;
	len = HELPER_SHORT;
	HK_CHECK(sysctl_long(outbuf, &len, NULL, 0, &lval) == 0);
	HK_CHECK(len == sizeof(long));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	reset_out(POISON);
	len = HELPER_SHORT;
	HK_CHECK(sysctl_rdlong(outbuf, &len, NULL, 1) == 0);
	HK_CHECK(len == sizeof(long));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	reset_out(POISON);
	len = 4;
	HK_CHECK(sysctl_string(outbuf, &len, NULL, 0, str, sizeof(str)) == 0);
	HK_CHECK(len == 9);
	HK_CHECK(outbuf[0] == 'h' && outbuf[3] == 't');
	HK_CHECK((unsigned char)outbuf[4] == POISON);

	reset_out(POISON);
	len = 4;
	HK_CHECK(sysctl_rdstring(outbuf, &len, NULL, "hostname") == 0);
	HK_CHECK(len == 9);
	HK_CHECK(outbuf[0] == 'h' && outbuf[3] == 't');
	HK_CHECK((unsigned char)outbuf[4] == POISON);

	reset_out(POISON);
	st.m_size = 5;
	st.m_addr = 6;
	len = HELPER_SHORT;
	HK_CHECK(sysctl_struct(outbuf, &len, NULL, 0, &st, sizeof(st)) == 0);
	HK_CHECK(len == sizeof(st));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	reset_out(POISON);
	len = HELPER_SHORT;
	HK_CHECK(sysctl_rdstruct(outbuf, &len, NULL, &st, sizeof(st)) == 0);
	HK_CHECK(len == sizeof(st));
	HK_CHECK((unsigned char)outbuf[HELPER_SHORT] == POISON);

	/* An exact buffer delivers the value whole. */
	reset_out(POISON);
	len = sizeof(int);
	HK_CHECK(sysctl_rdint(outbuf, &len, NULL, 0x0a0b0c0d) == 0);
	HK_CHECK(len == sizeof(int));
	bcopy(outbuf, &ival, sizeof(ival));
	HK_CHECK(ival == 0x0a0b0c0d);

	reset_out(POISON);
	len = sizeof(small);
	HK_CHECK(sysctl_rdstring(outbuf, &len, NULL, "abc") == 0);
	HK_CHECK(len == 4);
	HK_CHECK(hk_streq(outbuf, "abc"));
}

/*
 * The refusals that stand ahead of the value, which no bounded copy
 * reaches: a write to a read-only helper, and a new value whose length the
 * node cannot take. Each returns before the output buffer is touched and
 * without reporting a length.
 */
static void
test_helper_refusals(void)
{
	static char str[32] = "hostname";
	static struct mapent st;
	size_t len;
	int ival = 0, newval = 0;
	long lval = 0;

	reset_out(POISON);
	len = sizeof(outbuf);
	HK_CHECK(sysctl_rdint(outbuf, &len, &newval, 1) == EPERM);
	HK_CHECK(sysctl_rdlong(outbuf, &len, &newval, 1) == EPERM);
	HK_CHECK(sysctl_rdstring(outbuf, &len, &newval, "x") == EPERM);
	HK_CHECK(sysctl_rdstruct(outbuf, &len, &newval, &st, sizeof(st)) ==
	    EPERM);
	HK_CHECK(out_untouched());
	HK_CHECK(len == sizeof(outbuf));

	/* A new value of the wrong length is refused before any copy. */
	len = sizeof(outbuf);
	HK_CHECK(sysctl_int(outbuf, &len, &newval, 1, &ival) == EINVAL);
	HK_CHECK(sysctl_long(outbuf, &len, &newval, 1, &lval) == EINVAL);
	HK_CHECK(sysctl_string(outbuf, &len, &newval, sizeof(str), str,
	    sizeof(str)) == EINVAL);
	HK_CHECK(sysctl_struct(outbuf, &len, &newval, sizeof(st) + 1, &st,
	    sizeof(st)) == EINVAL);
	HK_CHECK(out_untouched());
	HK_CHECK(len == sizeof(outbuf));
}

/*
 * A write whose read-back buffer is short still writes. The bounded copy
 * reports no error, so the copyin that follows it runs and __sysctl() adds
 * ENOMEM afterwards: the caller sees the truncation of the old value and the
 * new value takes effect, which is the order patch 475 establishes.
 */
static void
test_write_survives_short_read_buffer(void)
{
	size_t len;
	int val = 1, newval = 99;

	reset_out(POISON);
	len = HELPER_SHORT;
	HK_CHECK(sysctl_int(outbuf, &len, &newval, sizeof(newval), &val) == 0);
	HK_CHECK(val == 99);
	HK_CHECK(len == sizeof(int));
}

struct structure_value {
	unsigned first;
	unsigned second;
};

static void
test_structure_replacement(void)
{
	struct structure_value replacement = { 71, 83 };
	struct structure_value value;
	const size_t input_lengths[] = {
		0, 1, sizeof(value) - 1, sizeof(value), sizeof(value) + 1
	};
	const size_t output_lengths[] = { 0, 1, sizeof(value), OUTBUF };
	size_t length, input_index, output_index, byte_index;
	int error;

	/* Full backing storage makes overreads a contract failure, not a fault. */
	for (input_index = 0; input_index < sizeof(input_lengths) /
	    sizeof(input_lengths[0]); input_index++) {
		for (output_index = 0; output_index < sizeof(output_lengths) /
		    sizeof(output_lengths[0]); output_index++) {
			reset_calls();
			reset_out(POISON);
			value.first = 11;
			value.second = 23;
			length = output_lengths[output_index];
			error = sysctl_struct(outbuf, &length, &replacement,
			    input_lengths[input_index], &value, sizeof(value));
			if (input_lengths[input_index] != sizeof(value)) {
				HK_CHECK(error == EINVAL);
				HK_CHECK(copyin_state.calls == 0);
				HK_CHECK(copyout_state.calls == 0);
				HK_CHECK(value.first == 11 && value.second == 23);
				HK_CHECK(length == output_lengths[output_index]);
				HK_CHECK(out_untouched());
			} else {
				struct structure_value original = { 11, 23 };
				size_t copied = MIN(sizeof(value),
				    output_lengths[output_index]);

				HK_CHECK(error == 0);
				HK_CHECK(copyin_state.calls == 1);
				HK_CHECK(copyin_state.extent == sizeof(value));
				HK_CHECK(copyout_state.calls == 1);
				HK_CHECK(copyout_state.extent == copied);
				HK_CHECK(value.first == 71 && value.second == 83);
				HK_CHECK(length == sizeof(value));
				for (byte_index = 0; byte_index < copied; byte_index++)
					HK_CHECK(outbuf[byte_index] ==
					    ((char *)&original)[byte_index]);
				for (; byte_index < OUTBUF; byte_index++)
					HK_CHECK((unsigned char)outbuf[byte_index] == POISON);
			}
		}
	}

	/* A null input is a read or size query, regardless of newlen. */
	for (input_index = 0; input_index < sizeof(input_lengths) /
	    sizeof(input_lengths[0]); input_index++) {
		reset_calls();
		value = replacement;
		length = 0;
		HK_CHECK(sysctl_struct(NULL, &length, NULL,
		    input_lengths[input_index], &value, sizeof(value)) == 0);
		HK_CHECK(length == sizeof(value));
		HK_CHECK(copyin_state.calls == 0 && copyout_state.calls == 0);
		reset_out(POISON);
		length = 1;
		HK_CHECK(sysctl_struct(outbuf, &length, NULL,
		    input_lengths[input_index], &value, sizeof(value)) == 0);
		HK_CHECK(length == sizeof(value));
		HK_CHECK(copyin_state.calls == 0 && copyout_state.calls == 1);
		HK_CHECK(copyout_state.extent == 1);
		HK_CHECK(outbuf[0] == ((char *)&replacement)[0]);
		HK_CHECK((unsigned char)outbuf[1] == POISON);
		HK_CHECK(value.first == 71 && value.second == 83);
	}

	reset_calls();
	value.first = 11;
	length = 0;
	HK_CHECK(sysctl_struct(NULL, &length, &replacement,
	    sizeof(replacement), &value, sizeof(value)) == 0);
	HK_CHECK(copyin_state.calls == 1 && copyout_state.calls == 0);
	HK_CHECK(copyin_state.extent == sizeof(value));
	HK_CHECK(value.first == 71 && value.second == 83);
	HK_CHECK(length == sizeof(value));
}

static void
test_structure_copy_faults(void)
{
	struct structure_value replacement = { 71, 83 };
	struct structure_value value = { 11, 23 };
	size_t length;

	reset_calls();
	reset_out(POISON);
	copyout_state.fail_at = 1;
	length = sizeof(outbuf);
	HK_CHECK(sysctl_struct(outbuf, &length, &replacement,
	    sizeof(replacement), &value, sizeof(value)) == EFAULT);
	HK_CHECK(copyout_state.calls == 1 && copyin_state.calls == 0);
	HK_CHECK(copyout_state.extent == sizeof(value));
	HK_CHECK(value.first == 11 && value.second == 23);
	HK_CHECK(out_untouched());
	HK_CHECK(length == sizeof(value));

	reset_calls();
	copyin_state.fail_at = 1;
	length = 1;
	HK_CHECK(sysctl_struct(outbuf, &length, &replacement,
	    sizeof(replacement), &value, sizeof(value)) == EFAULT);
	HK_CHECK(copyout_state.calls == 1 && copyin_state.calls == 1);
	HK_CHECK(copyout_state.extent == 1);
	HK_CHECK(copyin_state.extent == sizeof(value));
	HK_CHECK(value.first == 11 && value.second == 23);
	HK_CHECK(outbuf[0] == ((char *)&value)[0]);
	HK_CHECK((unsigned char)outbuf[1] == POISON);
	HK_CHECK(length == sizeof(value));
	reset_calls();
}

/*
 * __sysctl() end to end, which is where the length reaches the caller and
 * where truncation becomes ENOMEM. The argument block mirrors the syscall
 * ABI kern_sysctl.c reads out of u_arg.
 */
struct gate_sysctl_args {
	int	*name;
	u_int	 namelen;
	void	*old;
	size_t	*oldlenp;
	void	*new;
	size_t	 newlen;
};

static int
call_syscall(int *name, u_int namelen, void *old, size_t *oldlenp, void *new,
    size_t newlen)
{
	struct gate_sysctl_args args;

	args.name = name;
	args.namelen = namelen;
	args.old = old;
	args.oldlenp = oldlenp;
	args.new = new;
	args.newlen = newlen;
	bcopy(&args, u.u_arg, sizeof(args));
	u.u_error = 0;
	u.u_rval = 0;
	__sysctl();
	return (u.u_error);
}

static void
test_syscall_enomem(void)
{
	size_t len;
	int name[2];
	int error;

	reset_map();
	name[0] = CTL_VM;
	name[1] = VM_SWAPMAP;

	/* An ample buffer: no truncation, and the length is the value's. */
	reset_out(POISON);
	len = sizeof(outbuf);
	error = call_syscall(name, 2, outbuf, &len, NULL, 0);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);
	HK_CHECK(u.u_rval == 0);

	/*
	 * A short buffer: ENOMEM, and the length the caller reads back is
	 * what the value needs, so the next call can be sized from it.
	 */
	reset_out(POISON);
	len = PAYLOAD_LEN - sizeof(struct mapent);
	error = call_syscall(name, 2, outbuf, &len, NULL, 0);
	HK_CHECK(error == ENOMEM);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);

	/* Sized from that answer, the same call succeeds. */
	reset_out(POISON);
	error = call_syscall(name, 2, outbuf, &len, NULL, 0);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);

	/*
	 * A size query offers no buffer, so no length can be too short and
	 * ENOMEM never arises however small the number passed in.
	 */
	len = 0;
	error = call_syscall(name, 2, NULL, &len, NULL, 0);
	HK_CHECK(error == 0);
	HK_CHECK(len == (size_t)PAYLOAD_LEN);

	/* The node's own verdict stands ahead of the truncation check. */
	reset_out(POISON);
	len = 1;
	name[1] = 4;			/* the retired coremap id */
	error = call_syscall(name, 2, outbuf, &len, NULL, 0);
	HK_CHECK(error == EOPNOTSUPP);
}

static void
test_syscall_copy_faults(void)
{
	int name[2] = { CTL_KERN, KERN_HOSTID };
	long replacement = 83;
	size_t length;
	unsigned failed_call;

	/* Name and length faults stop dispatch before the node can mutate hostid. */
	for (failed_call = 1; failed_call <= 2; failed_call++) {
		reset_calls();
		reset_out(POISON);
		copyin_state.fail_at = failed_call;
		hostid = 23;
		length = sizeof(outbuf);
		HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
		    sizeof(replacement)) == EFAULT);
		HK_CHECK(copyin_state.calls == failed_call);
		HK_CHECK(copyin_state.extent ==
		    (failed_call == 1 ? sizeof(name) : sizeof(length)));
		HK_CHECK(copyout_state.calls == 0);
		HK_CHECK(out_untouched());
		HK_CHECK(length == sizeof(outbuf));
		HK_CHECK(hostid == 23);
		HK_CHECK(u.u_rval == 0);
	}

	/* A failed value copy stops the write; the length still reaches the caller. */
	reset_calls();
	copyout_state.fail_at = 1;
	length = sizeof(outbuf);
	HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
	    sizeof(replacement)) == EFAULT);
	HK_CHECK(copyin_state.calls == 2 && copyout_state.calls == 2);
	HK_CHECK(copyout_state.extent == sizeof(length));
	HK_CHECK(hostid == 23);
	HK_CHECK(out_untouched());
	HK_CHECK(length == sizeof(hostid));

	/* A new-value fault propagates after delivering the old value. */
	reset_calls();
	copyin_state.fail_at = 3;
	length = sizeof(outbuf);
	HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
	    sizeof(replacement)) == EFAULT);
	HK_CHECK(copyin_state.calls == 3 && copyout_state.calls == 2);
	HK_CHECK(copyin_state.extent == sizeof(replacement));
	HK_CHECK(outbuf[0] == ((char *)&hostid)[0]);
	HK_CHECK((unsigned char)outbuf[sizeof(hostid)] == POISON);
	HK_CHECK(hostid == 23);
	HK_CHECK(length == sizeof(hostid));

	/* A length-copy fault follows a successful node, including its write. */
	reset_calls();
	copyout_state.fail_at = 2;
	length = sizeof(outbuf);
	HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
	    sizeof(replacement)) == EFAULT);
	HK_CHECK(copyin_state.calls == 3 && copyout_state.calls == 2);
	HK_CHECK(copyout_state.extent == sizeof(length));
	HK_CHECK(hostid == replacement);
	HK_CHECK(length == sizeof(outbuf));
	HK_CHECK(u.u_rval == 0);

	/* A node refusal takes precedence over the later length-copy fault. */
	reset_calls();
	reset_out(POISON);
	copyout_state.fail_at = 1;
	length = sizeof(outbuf);
	name[1] = KERN_MAXID;
	HK_CHECK(call_syscall(name, 2, outbuf, &length, NULL, 0) == EOPNOTSUPP);
	HK_CHECK(copyin_state.calls == 2 && copyout_state.calls == 1);
	HK_CHECK(copyout_state.extent == sizeof(length));
	HK_CHECK(out_untouched());
	HK_CHECK(length == sizeof(outbuf));
	reset_calls();
}

static void
test_syscall_authorization(void)
{
	int name[2] = { CTL_KERN, KERN_HOSTID };
	long replacement = 83;
	long previous;
	size_t length;

	reset_calls();
	reset_out(POISON);
	allow_superuser = 0;
	hostid = 23;
	length = sizeof(outbuf);
	HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
	    sizeof(replacement)) == EPERM);
	HK_CHECK(superuser_calls == 1);
	HK_CHECK(copyin_state.calls == 0 && copyout_state.calls == 0);
	HK_CHECK(hostid == 23);
	HK_CHECK(length == sizeof(outbuf));
	HK_CHECK(out_untouched());
	HK_CHECK(u.u_rval == 0);

	/* Read-only calls bypass suser(), even with the denial double armed. */
	HK_CHECK(call_syscall(name, 2, outbuf, &length, NULL, 0) == 0);
	HK_CHECK(superuser_calls == 1);
	HK_CHECK(copyin_state.calls == 2 && copyout_state.calls == 2);
	HK_CHECK(length == sizeof(hostid));
	HK_CHECK(u.u_rval == 0);
	bcopy(outbuf, &previous, sizeof(previous));
	HK_CHECK(previous == 23);
	HK_CHECK(hostid == 23);

	reset_calls();
	reset_out(POISON);
	length = sizeof(outbuf);
	HK_CHECK(call_syscall(name, 2, outbuf, &length, &replacement,
	    sizeof(replacement)) == 0);
	HK_CHECK(superuser_calls == 1);
	HK_CHECK(copyin_state.calls == 3 && copyout_state.calls == 2);
	HK_CHECK(copyin_state.extent == sizeof(replacement));
	HK_CHECK(hostid == replacement);
	HK_CHECK(length == sizeof(hostid));
	HK_CHECK(u.u_rval == 0);
	bcopy(outbuf, &previous, sizeof(previous));
	HK_CHECK(previous == 23);
	HK_CHECK((unsigned char)outbuf[sizeof(hostid)] == POISON);
	reset_calls();
}

/*
 * Spell text into words as sys/sysctl.h defines the CPU_NLIST name and
 * return the component count: the bytes, a terminator, zero padding.
 */
static u_int
spell(const char *text, int *words)
{
	size_t length = 0, i;
	u_int count;

	while (text[length] != '\0')
		length++;
	count = (u_int)(length / CTL_NLIST_BYTES + 1);

	for (i = 0; i < count; i++)
		words[i] = 0;
	for (i = 0; i < length; i++)
		words[i / CTL_NLIST_BYTES] |= (int)((u_int)(unsigned char)text[i]
		    << (8 * (i % CTL_NLIST_BYTES)));
	return (count);
}

/* sysctl_nlist() on words, reading into an int the gate poisons first. */
static int
lookup(int *words, u_int count, int *value, size_t *length, void *newp)
{
	*value = -1;
	*length = sizeof(*value);
	return (sysctl_nlist(gate_symbols, words, count, value, length, newp));
}

static void
test_nlist_names(void)
{
	int words[CTL_MAXNAME], value, mib[CTL_MAXNAME];
	size_t length;
	u_int count;
	char token = 1;

	reset_calls();
	count = spell("_hz", words);
	HK_CHECK(count == 1);
	HK_CHECK(lookup(words, count, &value, &length, NULL) == 0);
	HK_CHECK(value == 11 && length == sizeof(int));
	count = spell("_abc", words);
	HK_CHECK(count == 2 && words[1] == 0);
	HK_CHECK(lookup(words, count, &value, &length, NULL) == 0);
	HK_CHECK(value == 33);
	count = spell("_dk_ndrive", words);
	HK_CHECK(count == 3);
	HK_CHECK(lookup(words, count, &value, &length, NULL) == 0);
	HK_CHECK(value == 22);

	/* A prefix or an extension of a symbol names nothing. */
	count = spell("_h", words);
	HK_CHECK(lookup(words, count, &value, &length, NULL) == EOPNOTSUPP);
	count = spell("_hzz", words);
	HK_CHECK(lookup(words, count, &value, &length, NULL) == EOPNOTSUPP);
	HK_CHECK(value == -1);

	/* No terminator: "_abc" in one component. */
	reset_calls();
	count = spell("_abc", words);
	HK_CHECK(lookup(words, 1, &value, &length, NULL) == EINVAL);
	/* The terminator in an earlier component, a zero component after it. */
	count = spell("_hz", words);
	words[1] = 0;
	HK_CHECK(lookup(words, 2, &value, &length, NULL) == EINVAL);
	/* A nonzero byte after the terminator. */
	count = spell("_abc", words);
	words[1] = 0x00ff0000;
	HK_CHECK(lookup(words, count, &value, &length, NULL) == EINVAL);
	/* No terminator: "_hz" with its fourth byte set. */
	count = spell("_hz", words);
	words[0] |= (int)0x01000000;
	HK_CHECK(lookup(words, count, &value, &length, NULL) == EINVAL);
	HK_CHECK(lookup(words, 0, &value, &length, NULL) == EINVAL);
	HK_CHECK(lookup(words, CTL_MAXNAME - 1, &value, &length, NULL) ==
	    EINVAL);
	HK_CHECK(value == -1 && copyout_state.calls == 0);

	/* The node takes no new value. */
	count = spell("_hz", words);
	HK_CHECK(lookup(words, count, &value, &length, &token) == EPERM);
	HK_CHECK(value == -1 && copyout_state.calls == 0);

	/* A short buffer takes the prefix and learns the full length. */
	value = -1;
	length = 2;
	HK_CHECK(sysctl_nlist(gate_symbols, words, count, &value, &length,
	    NULL) == 0);
	HK_CHECK(copyout_state.extent == 2 && length == sizeof(int));
	HK_CHECK((value & 0xffff) == 11);

	/* An unprivileged __sysctl() reaches the node without suser(). */
	reset_calls();
	allow_superuser = 0;
	mib[0] = CTL_MACHDEP;
	mib[1] = GATE_CPU_NLIST;
	count = spell("_dk_ndrive", mib + 2);
	value = -1;
	length = sizeof(value);
	HK_CHECK(call_syscall(mib, 2 + count, &value, &length, NULL, 0) == 0);
	HK_CHECK(superuser_calls == 0);
	HK_CHECK(value == 22 && length == sizeof(int));
	/* The same request with a new value is refused before the node. */
	HK_CHECK(call_syscall(mib, 2 + count, &value, &length, &token,
	    sizeof(token)) == EPERM);
	HK_CHECK(superuser_calls == 1);
	reset_calls();
}

/*
 * User-area reads. A process row needs fields from the user area, which is
 * resident kernel memory, a block on the ring swap device, or a SwapRAM
 * pool image. Every case below names the block the kernel must read and a
 * change the reader must notice across its sleeps.
 */
static union gate_block core_user[NPROC];	/* resident user areas, by slot */
static struct tty gate_tty;
static struct proc *hook_proc;

/* Logical swap block to device block, the fold of struct map (sys/map.h). */
static int
device_block(int logical)
{
	if (logical >= GATE_SWAP_END)
		return (logical - GATE_SWAP_END + GATE_SWAP_FIRST);
	return (logical);
}

static void
reset_procs(void)
{
	bzero(proc, sizeof(proc));
	bzero(swapdisk, sizeof(swapdisk));
	bzero(core_user, sizeof(core_user));
	swapmap->m_ring_first = GATE_SWAP_FIRST;
	swapmap->m_ring_end = GATE_SWAP_END;
	swap_error = 0;
	swap_reads = 0;
	geteblk_calls = 0;
	brelse_calls = 0;
	during_geteblk = NULL;
	during_io = NULL;
	hook_proc = NULL;
#ifdef SWAPRAM
	bzero(pool_present, sizeof(pool_present));
	bzero(pool_user, sizeof(pool_user));
	pool_error = 0;
#endif
}

/*
 * A user area whose every reported field derives from ruid, so a record
 * built from the wrong user area shows in each field.
 */
static void
fill_user(struct user *up, struct proc *p, uid_t ruid)
{
	unsigned i;

	up->u_procp = p;
	up->u_ruid = ruid;
	up->u_groups[0] = ruid + 1000;
	up->u_rgid = ruid + 2000;
	up->u_start = (long)(100000 + ruid);
	up->u_ru.ru_utime = (long)(ruid * 3);
	up->u_ru.ru_stime = (long)(ruid * 5);
	up->u_cru.ru_utime = 7;
	up->u_cru.ru_stime = 11;
	up->u_ttyp = &gate_tty;
	up->u_ttyd = (dev_t)(0x0400 + ruid);
	for (i = 0; i < sizeof(up->u_comm); i++)
		up->u_comm[i] = POISON;
	for (i = 0; i < sizeof(up->u_psargs); i++)
		up->u_psargs[i] = POISON;
	up->u_comm[0] = 'c';
	up->u_comm[1] = (char)('0' + ruid % 10);
	up->u_comm[2] = '\0';
	up->u_psargs[0] = 'a';
	up->u_psargs[1] = ' ';
	up->u_psargs[2] = (char)('0' + ruid % 10);
	up->u_psargs[3] = '\0';
}

static struct proc *
make_resident(int slot, int pid, uid_t ruid)
{
	struct proc *p = &proc[slot];

	p->p_stat = SRUN;
	p->p_pid = (short)pid;
	p->p_flag = SLOAD;
	p->p_addr = (size_t)&core_user[slot].u;
	fill_user(&core_user[slot].u, p, ruid);
	return (p);
}

static struct proc *
make_on_flash(int slot, int pid, int logical, uid_t ruid)
{
	struct proc *p = &proc[slot];

	p->p_stat = SSLEEP;
	p->p_pid = (short)pid;
	p->p_flag = 0;
	p->p_addr = (size_t)logical;
	fill_user(&swapdisk[device_block(logical)].u, p, ruid);
	return (p);
}

/* The real uid fill_from_u() reports, and whether it left the buffer balanced. */
static uid_t
read_ruid(struct proc *p, dev_t *ttyd, struct tty **ttyp)
{
	uid_t ruid = 0;

	fill_from_u(p, &ruid, ttyp, ttyd);
	HK_CHECK(geteblk_calls == brelse_calls);
	return (ruid);
}

static void
test_uarea_resident(void)
{
	struct tty *ttyp;
	dev_t ttyd;

	reset_procs();
	HK_CHECK(read_ruid(make_resident(3, 30, 123), &ttyd, &ttyp) == 123);
	HK_CHECK(ttyd == 0x0400 + 123 && ttyp == &gate_tty);
	HK_CHECK(geteblk_calls == 0 && swap_reads == 0);
}

/*
 * Logical block GATE_SWAP_END + 2 is device block GATE_SWAP_FIRST + 2. A
 * reader that seeks the logical block reads past the device and gets the
 * poisoned buffer back.
 */
static void
test_uarea_folded_flash(void)
{
	struct proc *p;
	struct tty *ttyp;
	dev_t ttyd;

	reset_procs();
	p = make_on_flash(5, 50, GATE_SWAP_END + 2, 321);
	HK_CHECK(read_ruid(p, &ttyd, &ttyp) == 321);
	HK_CHECK(ttyd == 0x0400 + 321 && ttyp == &gate_tty);
	HK_CHECK(geteblk_calls == 1 && swap_reads == 1);

	reset_procs();
	HK_CHECK(read_ruid(make_on_flash(5, 50, 9, 222), NULL, NULL) == 222);
}

/* Another process's image now holds the block: the read is refused. */
static void
test_uarea_overwritten(void)
{
	struct proc *p;
	struct tty *ttyp = &gate_tty;
	dev_t ttyd = 0;

	reset_procs();
	p = make_on_flash(5, 50, GATE_SWAP_END + 2, 321);
	swapdisk[device_block(GATE_SWAP_END + 2)].u.u_procp = &proc[6];
	HK_CHECK(read_ruid(p, &ttyd, &ttyp) == (uid_t)-2);
	HK_CHECK(ttyd == NODEV && ttyp == NULL);
}

static void
exit_process(void)
{
	hook_proc->p_stat = SZOMB;
}

static void
free_slot(void)
{
	hook_proc->p_stat = 0;
}

static void
reuse_slot(void)
{
	hook_proc->p_pid++;
}

static void
swap_in(void)
{
	hook_proc->p_flag |= SLOAD;
	hook_proc->p_addr = (size_t)&core_user[hook_proc - proc].u;
	fill_user(&core_user[hook_proc - proc].u, hook_proc, 77);
}

/* Each change while the reader sleeps is noticed after the sleep. */
static void
test_uarea_changes_while_asleep(void)
{
	reset_procs();
	hook_proc = make_on_flash(5, 50, 9, 321);
	during_io = exit_process;
	HK_CHECK(read_ruid(hook_proc, NULL, NULL) == (uid_t)-2);

	reset_procs();
	hook_proc = make_on_flash(5, 50, 9, 321);
	during_io = reuse_slot;
	HK_CHECK(read_ruid(hook_proc, NULL, NULL) == (uid_t)-2);

	reset_procs();
	hook_proc = make_on_flash(5, 50, 9, 321);
	during_geteblk = free_slot;
	HK_CHECK(read_ruid(hook_proc, NULL, NULL) == (uid_t)-2);
	HK_CHECK(swap_reads == 0);

	/* A process back in core is read in place, from its new user area. */
	reset_procs();
	hook_proc = make_on_flash(5, 50, 9, 321);
	during_io = swap_in;
	HK_CHECK(read_ruid(hook_proc, NULL, NULL) == 77);

	reset_procs();
	hook_proc = make_on_flash(5, 50, 9, 321);
	during_geteblk = swap_in;
	HK_CHECK(read_ruid(hook_proc, NULL, NULL) == 77);
	HK_CHECK(swap_reads == 0);
}

static void
test_uarea_unreadable(void)
{
	struct proc *p;

	reset_procs();
	p = make_on_flash(5, 50, 9, 321);
	swap_error = 1;
	HK_CHECK(read_ruid(p, NULL, NULL) == (uid_t)-2);

	/* A zombie has no user area, and the reader takes no buffer for it. */
	reset_procs();
	p = make_resident(3, 30, 123);
	p->p_stat = SZOMB;
	HK_CHECK(read_ruid(p, NULL, NULL) == (uid_t)-2);
	HK_CHECK(geteblk_calls == 0);
}

#ifdef SWAPRAM
/* A pooled image has a zero p_addr; the pool decoder supplies its user area. */
static void
test_uarea_pool(void)
{
	struct proc *p;

	reset_procs();
	p = make_on_flash(7, 70, 0, 999);
	pool_present[7] = 1;
	fill_user(&pool_user[7].u, p, 456);
	HK_CHECK(read_ruid(p, NULL, NULL) == 456);
	HK_CHECK(swap_reads == 0 && geteblk_calls == 1);

	pool_user[7].u.u_procp = &proc[8];
	HK_CHECK(read_ruid(p, NULL, NULL) == (uid_t)-2);

	fill_user(&pool_user[7].u, p, 456);
	pool_error = 1;
	HK_CHECK(read_ruid(p, NULL, NULL) == (uid_t)-2);
}
#endif

/*
 * KERN_PROC records. The table holds a process in every place an image can
 * live -- resident, on flash at a folded block, unfolded on flash, in the
 * pool when SWAPRAM is built, and exited -- plus an embryonic slot and the
 * caller. Each record must come from the user area of its own process,
 * carry no fixture address, and appear exactly once.
 */
#define REC		((int)sizeof(struct kinfo_proc))
#define CALLER_SLOT	1
#define CALLER_UID	500

static char records[NPROC * 256];
#define POISONED(i)	((unsigned char)records[(i)] == POISON)

static void
set_caller(uid_t euid, uid_t ruid)
{
	u.u_procp = &proc[CALLER_SLOT];
	proc[CALLER_SLOT].p_uid = euid;
	u.u_ruid = ruid;
}

static void
build_table(void)
{
	struct proc *p;

	reset_procs();
	gate_tty.t_pgrp = 42;
	p = make_resident(0, 0, 0);
	p->p_flag |= SSYS;
	make_resident(CALLER_SLOT, 1, CALLER_UID);
	p = make_on_flash(2, 12, GATE_SWAP_END + 2, CALLER_UID);
	p->p_uid = CALLER_UID;
	p->p_pgrp = 12;
	p = make_on_flash(3, 13, 9, 7);
	p->p_uid = 7;
	p->p_dsize = 4000;
	p->p_ssize = 2048;
	p->p_tsize = 1000;
	p->p_tip = (struct inode *)&gate_tty;
	p = &proc[4];
	p->p_stat = SZOMB;
	p->p_pid = 14;
	p->p_ru.ru_utime = 30;
	p->p_ru.ru_stime = 40;
	proc[5].p_stat = SIDL;
	proc[5].p_pid = 15;
#ifdef SWAPRAM
	p = make_on_flash(6, 16, 0, 999);
	pool_present[6] = 1;
	fill_user(&pool_user[6].u, p, 8);
	p->p_uid = 8;
#endif
	set_caller(CALLER_UID, CALLER_UID);
}

static int
get_records(int selector, int arg, int stride, size_t *len)
{
	int name[3];

	name[0] = selector;
	name[1] = arg;
	name[2] = stride;
	for (unsigned i = 0; i < sizeof(records); i++)
		records[i] = POISON;
	*len = sizeof(records);
	return (sysctl_doproc(name, 3, records, len));
}

static const struct kinfo_proc *
record_for(int pid, size_t len)
{
	const struct kinfo_proc *found = NULL;

	for (size_t off = 0; off + REC <= len; off += REC) {
		const struct kinfo_proc *kp = (const void *)(records + off);

		if (kp->kp_pid == pid) {
			HK_CHECK(found == NULL);
			found = kp;
		}
	}
	return (found);
}

/* True when no four bytes of the buffer equal a fixture address. */
static int
holds_no_address(size_t len)
{
	uint32_t word;

	for (size_t off = 0; off + sizeof(word) <= len; off++) {
		bcopy(records + off, &word, sizeof(word));
		if (word >= (uint32_t)(uintptr_t)proc &&
		    word < (uint32_t)(uintptr_t)(proc + NPROC))
			return (0);
		if (word >= (uint32_t)(uintptr_t)core_user &&
		    word < (uint32_t)(uintptr_t)(core_user + NPROC))
			return (0);
		if (word == (uint32_t)(uintptr_t)&gate_tty)
			return (0);
	}
	return (1);
}

static void
test_records_every_residency(void)
{
	const struct kinfo_proc *kp;
	size_t len;
	int expected = 5;

#ifdef SWAPRAM
	expected = 6;
#endif
	build_table();
	HK_CHECK(get_records(KERN_PROC_ALL, 0, REC, &len) == 0);
	HK_CHECK(len == (size_t)(expected * REC));
	HK_CHECK(holds_no_address(len));
	HK_CHECK(record_for(15, len) == NULL);
	HK_CHECK(geteblk_calls == brelse_calls);

	kp = record_for(0, len);
	HK_CHECK(kp != NULL && (kp->kp_flag & (SSYS | SLOAD)) == (SSYS | SLOAD));

	kp = record_for(12, len);
	HK_CHECK(kp != NULL && (kp->kp_flag & SLOAD) == 0);
	HK_CHECK(kp != NULL && kp->kp_size == REC && kp->kp_error == 0);
	HK_CHECK(kp != NULL && kp->kp_ruid == CALLER_UID &&
	    kp->kp_gid == CALLER_UID + 1000 && kp->kp_rgid == CALLER_UID + 2000);
	HK_CHECK(kp != NULL && kp->kp_start == 100000 + CALLER_UID);
	HK_CHECK(kp != NULL && kp->kp_utime == CALLER_UID * 3 &&
	    kp->kp_stime == CALLER_UID * 5 && kp->kp_ctime == 18);
	HK_CHECK(kp != NULL && kp->kp_tdev == 0x0400 + CALLER_UID &&
	    kp->kp_tpgid == 42 && kp->kp_pgid == 12);
	HK_CHECK(kp != NULL && hk_streq(kp->kp_comm, "c0") &&
	    hk_streq(kp->kp_args, "a 0"));
	HK_CHECK(kp != NULL && kp->kp_comm[KI_COMMSZ - 1] == '\0' &&
	    kp->kp_args[KI_ARGSZ - 1] == '\0');
	HK_CHECK(kp != NULL && kp->kp_valid == (KP_VALID_LIVE |
	    KP_VALID_TIMES | KP_VALID_CRED | KP_VALID_START | KP_VALID_TTY |
	    KP_VALID_COMM | KP_VALID_ARGS));

	kp = record_for(13, len);
	HK_CHECK(kp != NULL && kp->kp_ruid == 7 && hk_streq(kp->kp_comm, "c7"));
	HK_CHECK(kp != NULL && kp->kp_dsize == 4000 && kp->kp_ssize == 2048 &&
	    kp->kp_tsize == 1000);

	kp = record_for(14, len);
	HK_CHECK(kp != NULL && kp->kp_stat == SZOMB);
	HK_CHECK(kp != NULL && kp->kp_valid == KP_VALID_TIMES &&
	    kp->kp_utime == 30 && kp->kp_stime == 40 && kp->kp_ctime == 0);
	HK_CHECK(kp != NULL && kp->kp_tdev == NODEV);

#ifdef SWAPRAM
	kp = record_for(16, len);
	HK_CHECK(kp != NULL && (kp->kp_flag & SLOAD) == 0 &&
	    kp->kp_ruid == 8 && hk_streq(kp->kp_comm, "c8"));
#endif
}

/*
 * Arguments go to root and to a caller whose effective uid is the
 * process's real or effective uid; anyone else gets the row without them.
 */
static void
test_records_argument_policy(void)
{
	const struct kinfo_proc *kp;
	size_t len;

	build_table();
	set_caller(CALLER_UID, CALLER_UID);
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && !(kp->kp_valid & KP_VALID_ARGS) &&
	    kp->kp_args[0] == '\0');
	HK_CHECK(kp != NULL && (kp->kp_valid & KP_VALID_COMM));

	build_table();
	set_caller(0, CALLER_UID);
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && (kp->kp_valid & KP_VALID_ARGS) &&
	    hk_streq(kp->kp_args, "a 7"));

	/* A real-uid match alone is not enough. */
	build_table();
	set_caller(CALLER_UID, 7);
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && !(kp->kp_valid & KP_VALID_ARGS));

	build_table();
	set_caller(7, CALLER_UID);
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && (kp->kp_valid & KP_VALID_ARGS));
}

/* A user area the kernel cannot read leaves the row, marked. */
static void
test_records_keep_unreadable_rows(void)
{
	const struct kinfo_proc *kp;
	size_t len;

	build_table();
	swap_error = 1;
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && kp->kp_error == EIO);
	HK_CHECK(kp != NULL && kp->kp_valid == KP_VALID_LIVE);
	HK_CHECK(kp != NULL && kp->kp_uid == 7 && kp->kp_comm[0] == '\0');
	HK_CHECK(kp != NULL && kp->kp_tdev == NODEV);

	build_table();
	hook_proc = &proc[3];
	during_io = reuse_slot;
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	kp = record_for(13, len);
	HK_CHECK(kp != NULL && kp->kp_error == ESRCH &&
	    kp->kp_valid == KP_VALID_LIVE);
	during_io = NULL;
}

/*
 * The kernel selects all processes or one pid; the pid test comes before
 * the user-area read, so one pid on flash costs one read.
 */
static void
test_records_selectors(void)
{
	size_t len;

	build_table();
	HK_CHECK(get_records(KERN_PROC_PID, 13, REC, &len) == 0);
	HK_CHECK(len == (size_t)REC && record_for(13, len));
	HK_CHECK(swap_reads == 1);
	HK_CHECK(get_records(KERN_PROC_PID, 14, REC, &len) == 0);
	HK_CHECK(len == (size_t)REC && record_for(14, len));
	HK_CHECK(get_records(KERN_PROC_PID, 15, REC, &len) == 0);
	HK_CHECK(len == 0);
	HK_CHECK(get_records(KERN_PROC_PID, 99, REC, &len) == 0);
	HK_CHECK(len == 0);
	for (int selector = 2; selector <= 7; selector++)
		HK_CHECK(get_records(selector, 0, REC, &len) == EINVAL);
	HK_CHECK(get_records(-1, 0, REC, &len) == EINVAL);
	HK_CHECK(get_records(KERN_PROC_ALL, 0, KI_PROC_MINSIZE - 1, &len) ==
	    EINVAL);
	HK_CHECK(get_records(KERN_PROC_ALL, 0, DEV_BSIZE + 1, &len) == EINVAL);
}

/*
 * The caller's record size sets the stride: a smaller one takes each
 * record's prefix, a larger one leaves the tail untouched. A size query
 * and a short buffer both report room for a full table.
 */
static void
test_records_caller_sized(void)
{
	int name[3] = { KERN_PROC_ALL, 0, REC };
	const struct kinfo_proc *kp;
	size_t len;
	int rows;

	build_table();
	HK_CHECK(get_records(KERN_PROC_ALL, 0, REC, &len) == 0);
	rows = (int)len / REC;

	HK_CHECK(get_records(KERN_PROC_ALL, 0, 8, &len) == 0);
	HK_CHECK(len == (size_t)(rows * 8));
	for (int r = 0; r < rows; r++) {
		kp = (const void *)(records + r * 8);
		HK_CHECK(kp->kp_size == REC);
	}
	HK_CHECK(POISONED(rows * 8));

	HK_CHECK(get_records(KERN_PROC_ALL, 0, REC + 12, &len) == 0);
	HK_CHECK(len == (size_t)(rows * (REC + 12)));
	for (int r = 0; r < rows; r++) {
		HK_CHECK(POISONED(r * (REC + 12) + REC));
		HK_CHECK(POISONED(r * (REC + 12) + REC + 11));
	}

	len = 0;
	HK_CHECK(sysctl_doproc(name, 3, NULL, &len) == 0);
	HK_CHECK(len == (size_t)(NPROC * REC));
	HK_CHECK(geteblk_calls == brelse_calls);

	/* Slots 0 and 1 are resident; the full buffer stops before slot 2's read. */
	swap_reads = 0;
	len = (size_t)(2 * REC + REC / 2);
	HK_CHECK(sysctl_doproc(name, 3, records, &len) == 0);
	HK_CHECK(len == (size_t)(NPROC * REC));
	HK_CHECK(swap_reads == 0);

	HK_CHECK(sysctl_doproc(name, 2, records, &len) == EINVAL);
	HK_CHECK(sysctl_doproc(name, 4, records, &len) == EINVAL);
}

/* The process table is read-only through __sysctl(). */
static void
test_records_refuse_writes(void)
{
	int mib[5] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0, REC };
	size_t len = sizeof(records);
	int token = 1;

	build_table();
	reset_calls();
	HK_CHECK(call_syscall(mib, 5, records, &len, &token, sizeof(token)) ==
	    EPERM);
	reset_calls();
	len = sizeof(records);
	HK_CHECK(call_syscall(mib, 5, records, &len, NULL, 0) == 0);
	HK_CHECK(superuser_calls == 0);
	HK_CHECK(len > 0 && u.u_rval == 0);
	reset_calls();
}

/*
 * KERN_FILE records. Process number k of the fixture gets a working
 * directory and three descriptors: a regular file at 0, a pipe at 3 and a
 * socket-typed entry at NOFILE - 1 whose f_data is no inode. Every value
 * derives from k, so a record built from another process's user area
 * shows in each field.
 */
#define FREC		((int)sizeof(struct kinfo_file))
#define GATE_DEV	0x0305

static void
give_files(int k, struct user *up)
{
	struct inode *dir = &inode[3 * k];
	struct inode *reg = dir + 1;
	struct inode *pipe = dir + 2;
	struct file *f = &file[3 * k];

	HK_CHECK(3 * k + 2 < NFILE && 3 * k + 2 < NINODE);
	dir->i_number = (ino_t)(100 + k);
	dir->i_mode = IFDIR | 0755;
	dir->i_size = 512;
	reg->i_number = (ino_t)(200 + k);
	reg->i_mode = IFREG | 0644;
	reg->i_size = 1000 + k;
	pipe->i_number = (ino_t)(300 + k);
	pipe->i_mode = IFREG;
	pipe->i_size = 7;
#ifndef SINGLE_UFS_ROOT
	dir->i_dev = reg->i_dev = pipe->i_dev = GATE_DEV;
#endif
	f[0].f_type = DTYPE_INODE;
	f[0].f_flag = FREAD;
	f[0].f_count = 1;
	f[0].f_offset = 40 + k;
	f[0].f_data = (caddr_t)reg;
	f[1].f_type = DTYPE_PIPE;
	f[1].f_flag = FREAD | FWRITE;
	f[1].f_count = 1;
	f[1].f_data = (caddr_t)pipe;
	f[2].f_type = DTYPE_SOCKET;
	f[2].f_flag = FWRITE;
	f[2].f_count = 1;
	f[2].f_data = (caddr_t)&gate_tty;
	up->u_cdir = dir;
	up->u_ofile[0] = &f[0];
	up->u_ofile[3] = &f[1];
	up->u_ofile[NOFILE - 1] = &f[2];
}

/* The process table of build_table(), with files for pids 1, 12, 13 and 16. */
static void
build_file_table(void)
{
	build_table();
	bzero(file, sizeof(file));
	bzero(inode, sizeof(inode));
	mount[0].m_dev = GATE_DEV;
	give_files(0, &core_user[CALLER_SLOT].u);
	give_files(1, &swapdisk[device_block(GATE_SWAP_END + 2)].u);
	give_files(2, &swapdisk[device_block(9)].u);
#ifdef SWAPRAM
	give_files(3, &pool_user[6].u);
#endif
}

static int
get_files(int selector, int pid, int stride, size_t *len)
{
	int name[3];

	name[0] = selector;
	name[1] = pid;
	name[2] = stride;
	for (unsigned i = 0; i < sizeof(records); i++)
		records[i] = POISON;
	*len = sizeof(records);
	return (sysctl_dofile(name, 3, records, len));
}

/* True when no four bytes of the buffer address a file or inode entry. */
static int
files_hold_no_address(size_t len)
{
	uint32_t word;

	for (size_t off = 0; off + sizeof(word) <= len; off++) {
		bcopy(records + off, &word, sizeof(word));
		if (word >= (uint32_t)(uintptr_t)file &&
		    word < (uint32_t)(uintptr_t)(file + NFILE))
			return (0);
		if (word >= (uint32_t)(uintptr_t)inode &&
		    word < (uint32_t)(uintptr_t)(inode + NINODE))
			return (0);
		if (word == (uint32_t)(uintptr_t)&gate_tty)
			return (0);
	}
	return (holds_no_address(len));
}

/* The four records give_files(k) produces, in descriptor order. */
static void
check_files_of(int k, int pid, size_t len)
{
	const struct kinfo_file *kf = (const void *)records;

	HK_CHECK(len == (size_t)(4 * FREC));
	HK_CHECK(files_hold_no_address(len));
	for (int r = 0; r < 4; r++) {
		HK_CHECK(kf[r].kf_size == FREC && kf[r].kf_pid == pid &&
		    kf[r].kf_spare == 0);
	}
	HK_CHECK(kf[0].kf_fd == KF_FD_CWD && kf[0].kf_type == DTYPE_INODE);
	HK_CHECK(kf[0].kf_valid == KF_VALID_INODE && kf[0].kf_flag == 0);
	HK_CHECK(kf[0].kf_inum == (uint32_t)(100 + k) &&
	    kf[0].kf_mode == (IFDIR | 0755) && kf[0].kf_isize == 512 &&
	    kf[0].kf_dev == GATE_DEV);
	HK_CHECK(kf[1].kf_fd == 0 && kf[1].kf_type == DTYPE_INODE);
	HK_CHECK(kf[1].kf_valid == (KF_VALID_FILE | KF_VALID_INODE));
	HK_CHECK(kf[1].kf_flag == FREAD && kf[1].kf_offset == 40 + k);
	HK_CHECK(kf[1].kf_inum == (uint32_t)(200 + k) &&
	    kf[1].kf_mode == (IFREG | 0644) && kf[1].kf_isize == 1000 + k &&
	    kf[1].kf_dev == GATE_DEV);
	HK_CHECK(kf[2].kf_fd == 3 && kf[2].kf_type == DTYPE_PIPE);
	HK_CHECK(kf[2].kf_valid == (KF_VALID_FILE | KF_VALID_INODE));
	HK_CHECK(kf[2].kf_flag == (FREAD | FWRITE) &&
	    kf[2].kf_inum == (uint32_t)(300 + k) && kf[2].kf_isize == 7);
	HK_CHECK(kf[3].kf_fd == NOFILE - 1 && kf[3].kf_type == DTYPE_SOCKET);
	HK_CHECK(kf[3].kf_valid == KF_VALID_FILE && kf[3].kf_flag == FWRITE);
	HK_CHECK(kf[3].kf_inum == 0 && kf[3].kf_mode == 0 &&
	    kf[3].kf_dev == 0 && kf[3].kf_isize == 0);
	HK_CHECK(POISONED(len));
	HK_CHECK(geteblk_calls == brelse_calls);
}

/* Every place an image lives yields its own descriptors. */
static void
test_files_every_residency(void)
{
	size_t len;

	build_file_table();
	set_caller(0, 0);
	HK_CHECK(get_files(KERN_FILE_BYPID, 1, FREC, &len) == 0);
	check_files_of(0, 1, len);
	HK_CHECK(geteblk_calls == 0);

	HK_CHECK(get_files(KERN_FILE_BYPID, 12, FREC, &len) == 0);
	check_files_of(1, 12, len);
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == 0);
	check_files_of(2, 13, len);
#ifdef SWAPRAM
	HK_CHECK(get_files(KERN_FILE_BYPID, 16, FREC, &len) == 0);
	check_files_of(3, 16, len);
#endif
	/* pid 0 has a user area but no files or directory in the fixture. */
	HK_CHECK(get_files(KERN_FILE_BYPID, 0, FREC, &len) == 0);
	HK_CHECK(len == 0 && POISONED(0));
}

/*
 * The records go to root and to a caller whose effective uid is the
 * process's real or effective uid, the rule KERN_PROC applies to arguments.
 */
static void
test_files_policy(void)
{
	size_t len;

	build_file_table();
	set_caller(CALLER_UID, CALLER_UID);
	HK_CHECK(get_files(KERN_FILE_BYPID, 12, FREC, &len) == 0);
	check_files_of(1, 12, len);
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == EPERM);
	HK_CHECK(POISONED(0) && geteblk_calls == brelse_calls);

	set_caller(7, CALLER_UID);
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == 0);
	check_files_of(2, 13, len);

	/* A real-uid match alone is not enough. */
	set_caller(CALLER_UID, 7);
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == EPERM);

	/* The process's real uid admits a caller its effective uid does not. */
	build_file_table();
	proc[2].p_uid = 9;
	set_caller(CALLER_UID, CALLER_UID);
	HK_CHECK(get_files(KERN_FILE_BYPID, 12, FREC, &len) == 0);
	check_files_of(1, 12, len);
}

/* No process, an embryo, a zombie, a failed read and a changed slot. */
static void
test_files_refusals(void)
{
	int name[3] = { KERN_FILE_BYPID, 13, FREC };
	size_t len;

	build_file_table();
	set_caller(0, 0);
	HK_CHECK(get_files(KERN_FILE_BYPID, 99, FREC, &len) == ESRCH);
	HK_CHECK(get_files(KERN_FILE_BYPID, 15, FREC, &len) == ESRCH);
	HK_CHECK(get_files(KERN_FILE_BYPID, 14, FREC, &len) == 0);
	HK_CHECK(len == 0 && POISONED(0) && geteblk_calls == 0);

	swap_error = 1;
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == EIO);
	HK_CHECK(POISONED(0) && geteblk_calls == brelse_calls);
	swap_error = 0;

	hook_proc = &proc[3];
	during_io = reuse_slot;
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == ESRCH);
	HK_CHECK(POISONED(0) && geteblk_calls == brelse_calls);
	during_io = NULL;

	/* A block another image now holds is not this process's table. */
	build_file_table();
	set_caller(0, 0);
	swapdisk[device_block(9)].u.u_procp = &proc[6];
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, FREC, &len) == ESRCH);

	for (int selector = -1; selector <= 4; selector++) {
		if (selector != KERN_FILE_BYPID)
			HK_CHECK(get_files(selector, 13, FREC, &len) == EINVAL);
	}
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, KI_FILE_MINSIZE - 1, &len) ==
	    EINVAL);
	HK_CHECK(get_files(KERN_FILE_BYPID, 13, DEV_BSIZE + 1, &len) == EINVAL);
	len = sizeof(records);
	HK_CHECK(sysctl_dofile(name, 2, records, &len) == EINVAL);
	HK_CHECK(sysctl_dofile(name, 4, records, &len) == EINVAL);
}

/*
 * The caller's record size sets the stride, as for KERN_PROC. A size query
 * and a short buffer both report room for the directory and NOFILE
 * descriptors.
 */
static void
test_files_caller_sized(void)
{
	int name[3] = { KERN_FILE_BYPID, 12, FREC };
	const struct kinfo_file *kf;
	size_t len;

	build_file_table();
	set_caller(0, 0);
	HK_CHECK(get_files(KERN_FILE_BYPID, 12, 8, &len) == 0);
	HK_CHECK(len == 4 * 8);
	for (int r = 0; r < 4; r++) {
		kf = (const void *)(records + r * 8);
		HK_CHECK(kf->kf_size == FREC && kf->kf_pid == 12);
	}
	HK_CHECK(POISONED(4 * 8));

	HK_CHECK(get_files(KERN_FILE_BYPID, 12, FREC + 12, &len) == 0);
	HK_CHECK(len == (size_t)(4 * (FREC + 12)));
	for (int r = 0; r < 4; r++) {
		kf = (const void *)(records + r * (FREC + 12));
		HK_CHECK(kf->kf_size == FREC && POISONED(r * (FREC + 12) + FREC) &&
		    POISONED(r * (FREC + 12) + FREC + 11));
	}

	len = 0;
	HK_CHECK(sysctl_dofile(name, 3, NULL, &len) == 0);
	HK_CHECK(len == (size_t)((NOFILE + 1) * FREC));

	len = (size_t)(2 * FREC + FREC / 2);
	for (unsigned i = 0; i < sizeof(records); i++)
		records[i] = POISON;
	HK_CHECK(sysctl_dofile(name, 3, records, &len) == 0);
	HK_CHECK(len == (size_t)((NOFILE + 1) * FREC));
	HK_CHECK(POISONED(2 * FREC) && geteblk_calls == brelse_calls);
}

/* The descriptor records are read-only through __sysctl(). */
static void
test_files_refuse_writes(void)
{
	int mib[5] = { CTL_KERN, KERN_FILE, KERN_FILE_BYPID, 1, FREC };
	size_t len = sizeof(records);
	int token = 1;

	build_file_table();
	reset_calls();
	HK_CHECK(call_syscall(mib, 5, records, &len, &token, sizeof(token)) ==
	    EPERM);
	reset_calls();
	len = sizeof(records);
	HK_CHECK(call_syscall(mib, 5, records, &len, NULL, 0) == 0);
	HK_CHECK(len == (size_t)(4 * FREC) && superuser_calls == 0);
	HK_CHECK(u.u_rval == 0);

	/* A size query reports through the length alone and returns 0. */
	reset_calls();
	len = 0;
	HK_CHECK(call_syscall(mib, 5, NULL, &len, NULL, 0) == 0);
	HK_CHECK(len == (size_t)((NOFILE + 1) * FREC) && u.u_rval == 0);
	reset_calls();
}

/*
 * VM_STATS: every counter lands in its own field. Each fixture value is
 * distinct and derives from the field's place, so a value copied into the
 * wrong field, or one counter copied twice, shows.
 */
static void
fill_counters(void)
{
	unsigned i;

	for (i = 0; i < CPUSTATES; i++)
		cp_time[i] = (long)(10 + i);
	rate.v_swtch = 101; rate.v_trap = 102; rate.v_syscall = 103;
	rate.v_intr = 104; rate.v_soft = 105; rate.v_fpsim = 106;
	rate.v_kbin = 107; rate.v_kbout = 108; rate.v_swpin = 109;
	rate.v_swpout = 110;
	sum.v_swtch = 201; sum.v_trap = 202; sum.v_syscall = 203;
	sum.v_intr = 204; sum.v_soft = 205; sum.v_fpsim = 206;
	sum.v_kbin = 207; sum.v_kbout = 208; sum.v_swpin = 209;
	sum.v_swpout = 210;
	forkstat.cntfork = 301; forkstat.cntvfork = 302;
	forkstat.sizfork = 303; forkstat.sizvfork = 304;
	nchstats.ncs_goodhits = 401; nchstats.ncs_badhits = 402;
	nchstats.ncs_falsehits = 403; nchstats.ncs_miss = 404;
	nchstats.ncs_long = 405; nchstats.ncs_pass2 = 406;
	nchstats.ncs_2passes = 407;
	tk_nin = 501;
	tk_nout = 502;
}

static void
poison_bytes(void *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		((unsigned char *)p)[i] = POISON;
}

static int
events_are(const struct kinfo_vmevents *e, uint32_t base)
{
	return (e->ve_swtch == base + 1 && e->ve_trap == base + 2 &&
	    e->ve_syscall == base + 3 && e->ve_intr == base + 4 &&
	    e->ve_soft == base + 5 && e->ve_fpsim == base + 6 &&
	    e->ve_kbin == base + 7 && e->ve_kbout == base + 8 &&
	    e->ve_swpin == base + 9 && e->ve_swpout == base + 10);
}

static void
test_vmstats(void)
{
	int mib[2] = { CTL_VM, VM_STATS };
	struct kinfo_vmstat vs;
	size_t len;
	int token = 1;

	fill_counters();
	reset_calls();
	poison_bytes(&vs, sizeof(vs));
	len = sizeof(vs);
	HK_CHECK(call_syscall(mib, 2, &vs, &len, NULL, 0) == 0);
	HK_CHECK(len == sizeof(vs) && u.u_rval == 0);
	HK_CHECK(vs.vs_size == sizeof(vs) && vs.vs_spare == 0);
	for (unsigned i = 0; i < KI_CPUSTATES; i++)
		HK_CHECK(vs.vs_cp_time[i] == 10 + i);
	HK_CHECK(events_are(&vs.vs_rate, 100));
	HK_CHECK(events_are(&vs.vs_sum, 200));
	HK_CHECK(vs.vs_cntfork == 301 && vs.vs_cntvfork == 302 &&
	    vs.vs_sizfork == 303 && vs.vs_sizvfork == 304);
	HK_CHECK(vs.vs_nch_goodhits == 401 && vs.vs_nch_badhits == 402 &&
	    vs.vs_nch_falsehits == 403 && vs.vs_nch_miss == 404 &&
	    vs.vs_nch_long == 405 && vs.vs_nch_pass2 == 406 &&
	    vs.vs_nch_2passes == 407);
	HK_CHECK(vs.vs_tk_nin == 501 && vs.vs_tk_nout == 502);

	/* A size query and a short buffer report the record's length. */
	reset_calls();
	len = 0;
	HK_CHECK(call_syscall(mib, 2, NULL, &len, NULL, 0) == 0);
	HK_CHECK(len == sizeof(vs));
	reset_calls();
	poison_bytes(&vs, sizeof(vs));
	len = offsetof(struct kinfo_vmstat, vs_rate);
	HK_CHECK(call_syscall(mib, 2, &vs, &len, NULL, 0) == ENOMEM);
	HK_CHECK(len == sizeof(vs) && vs.vs_size == sizeof(vs));
	HK_CHECK((unsigned char)vs.vs_rate.ve_swtch == POISON);

	/* The record is read-only. */
	reset_calls();
	len = sizeof(vs);
	HK_CHECK(call_syscall(mib, 2, &vs, &len, &token, sizeof(token)) ==
	    EPERM);
	reset_calls();
}

/*
 * HW_DISKSTATS: a record for each claimed slot, up to the first unnamed
 * one, with the name copied and ended within KI_DISKNAMESZ.
 */
static void
test_diskstats(void)
{
	int mib[2] = { CTL_HW, HW_DISKSTATS };
	struct kinfo_disk kd[DK_NDRIVE + 1];
	char sd0[] = "sd0", longname[] = "abcdefghij", late[] = "late";
	size_t len;
	int token = 1;

	bzero(dk_name, sizeof(dk_name));
	reset_calls();
	len = sizeof(kd);
	HK_CHECK(call_syscall(mib, 2, kd, &len, NULL, 0) == 0);
	HK_CHECK(len == 0 && u.u_rval == 0);

	dk_name[0] = sd0;
	dk_xfer[0] = 11;
	dk_bytes[0] = 12;
	dk_name[1] = longname;
	dk_xfer[1] = 21;
	dk_bytes[1] = 22;
	dk_name[3] = late;	/* after the unnamed slot 2: not claimed */
	reset_calls();
	poison_bytes(kd, sizeof(kd));
	len = sizeof(kd);
	HK_CHECK(call_syscall(mib, 2, kd, &len, NULL, 0) == 0);
	HK_CHECK(len == 2 * sizeof(kd[0]));
	HK_CHECK(hk_streq(kd[0].kd_name, "sd0") && kd[0].kd_xfer == 11 &&
	    kd[0].kd_bytes == 12);
	HK_CHECK(kd[0].kd_name[KI_DISKNAMESZ - 1] == '\0');
	HK_CHECK(hk_streq(kd[1].kd_name, "abcdefg") && kd[1].kd_xfer == 21 &&
	    kd[1].kd_bytes == 22);
	HK_CHECK((unsigned char)kd[2].kd_name[0] == POISON);

	reset_calls();
	len = sizeof(kd);
	HK_CHECK(call_syscall(mib, 2, kd, &len, &token, sizeof(token)) ==
	    EPERM);
	bzero(dk_name, sizeof(dk_name));
	reset_calls();
}

int
main(void)
{
	test_helper_lengths();
	test_helper_refusals();
	test_write_survives_short_read_buffer();
	test_syscall_enomem();
	test_swapmap_length();
	test_swapmap_zero_length_buffer();
	test_swapmap_payload();
	test_swapmap_encloses_nothing_else();
	test_swapmap_short_buffer();
	test_swapmap_readonly();
	test_swapmap_query_matches_copy();
	test_swapmap_tracks_limit();
	test_nswap();
	test_loadavg_and_meter();
	test_name_space();
	test_structure_replacement();
	test_structure_copy_faults();
	test_syscall_copy_faults();
	test_syscall_authorization();
	test_nlist_names();
	test_uarea_resident();
	test_uarea_folded_flash();
	test_uarea_overwritten();
	test_uarea_changes_while_asleep();
	test_uarea_unreadable();
#ifdef SWAPRAM
	test_uarea_pool();
#endif
	test_records_every_residency();
	test_records_argument_policy();
	test_records_keep_unreadable_rows();
	test_records_selectors();
	test_records_caller_sized();
	test_records_refuse_writes();
	test_files_every_residency();
	test_files_policy();
	test_files_refusals();
	test_files_caller_sized();
	test_files_refuse_writes();
	test_vmstats();
	test_diskstats();
	return (hk_verdict("sysctl"));
}
