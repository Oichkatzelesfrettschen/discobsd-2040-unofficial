/*
 * Host gate for vm_sysctl() in sys/kern/kern_sysctl.c, the CTL_VM branch of
 * the sysctl(2) tree.
 *
 * The gate links the kernel source itself, so the boundary it measures is
 * the one the board enforces. Two properties decide the CTL_VM_SWAPMAP node,
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
struct vmtotal total;
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
const struct bdevsw bdevsw[1];

/* The destination of every copyout, and the byte the gate fills it with. */
#define OUTBUF		512
static char outbuf[OUTBUF];
static char outfill;

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
	bcopy(from, to, nbytes);
	return (0);
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	bcopy(from, to, nbytes);
	return (0);
}

int
suser(void)
{
	return (1);
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

struct buf *
geteblk(void)
{
	panic("sysctl_test: geteblk");
	return (NULL);
}

void
brelse(struct buf *bp)
{
	(void)bp;
	panic("sysctl_test: brelse");
}

void
biowait(struct buf *bp)
{
	(void)bp;
	panic("sysctl_test: biowait");
}

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	(void)name;
	(void)namelen;
	(void)oldp;
	(void)oldlenp;
	(void)newp;
	(void)newlen;
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
 * A buffer one byte short of the payload takes nothing. sysctl_rdstruct()
 * refuses before it copies, so a caller that sized its buffer from a stale
 * query reads its own bytes rather than a truncated map.
 */
static void
test_swapmap_short_buffer(void)
{
	size_t len;
	int error;

	reset_map();
	reset_out(POISON);
	len = PAYLOAD_LEN - 1;
	error = call_vm(VM_SWAPMAP, outbuf, &len, NULL);
	HK_CHECK(error == ENOMEM);
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

int
main(void)
{
	test_swapmap_length();
	test_swapmap_payload();
	test_swapmap_encloses_nothing_else();
	test_swapmap_short_buffer();
	test_swapmap_readonly();
	test_swapmap_query_matches_copy();
	test_swapmap_tracks_limit();
	test_nswap();
	test_loadavg_and_meter();
	test_name_space();
	return (hk_verdict("sysctl"));
}
