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
	HK_CHECK(u.u_rval == PAYLOAD_LEN);

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
	HK_CHECK(u.u_rval == sizeof(hostid));
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
	HK_CHECK(u.u_rval == sizeof(hostid));
	bcopy(outbuf, &previous, sizeof(previous));
	HK_CHECK(previous == 23);
	HK_CHECK((unsigned char)outbuf[sizeof(hostid)] == POISON);
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
	return (hk_verdict("sysctl"));
}
