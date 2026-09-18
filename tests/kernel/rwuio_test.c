/*
 * Host gate for the read, write, readv and writev entry points of
 * sys/kern/sys_generic.c, linked from the kernel source.
 *
 * rwuio() sums the vector lengths into the transfer count that u_rval
 * reports as an int and readv(2) declares as ssize_t, so the sum is bounded
 * by 0x7fffffff. The gate pins that bound at the limit, one below it and one
 * above it, for a single oversized vector and for an overflow spread across
 * vectors, and holds the kernel's verdict against a 64-bit reference sum
 * over an enumerated set of vectors. A rejected request must reach no file
 * operation and move no file offset, which is the property the file
 * operation stub records.
 *
 * The file operation is a stub that consumes what it is told to, and for the
 * interrupted case it does what sleep() does on the board: it sets u_error
 * and longjmps to u_qsave. The kernel's setjmp is the port's assembly
 * routine, so the Makefile compiles sys_generic.c with setjmp mapped onto the
 * compiler's __builtin_setjmp, and the stub answers it with
 * __builtin_longjmp while rwuio's frame is still live.
 *
 * Built again at -m32 as rwuio_test32, where off_t, size_t and u_int are all
 * four bytes as they are on the target; the wide build exercises the same
 * paths but the wraparound the check exists for is an ILP32 fact.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/kernel.h>

#ifdef HK_ILP32
_Static_assert(sizeof(size_t) == 4, "size_t is the target's four bytes");
_Static_assert(sizeof(off_t) == 4, "off_t is the target's four bytes");
_Static_assert(sizeof(u_int) == 4, "u_int is the target's four bytes");
_Static_assert(sizeof(long) == 4, "long is the target's four bytes");
#endif

#define LIMIT		0x7fffffffU
#define FD		3
#define MAXVEC		16

struct user u;
struct timeval time;
int lbolt;
int usechz;

/* What the stub file operation was asked, and what it did. */
static unsigned rw_calls;
static u_int rw_resid_seen;
static enum uio_rw rw_direction;
static u_int rw_consume;	/* bytes the stub transfers before returning */
static int rw_error;		/* what the stub returns */
static int rw_interrupt;	/* the stub longjmps like sleep() after consuming */

static struct file fp;

static int
stub_rw(struct file *f, struct uio *uio)
{
	u_int n;

	rw_calls++;
	rw_resid_seen = uio->uio_resid;
	rw_direction = uio->uio_rw;
	n = rw_consume < uio->uio_resid ? rw_consume : uio->uio_resid;
	uio->uio_resid -= n;
	f->f_offset += n;
	if (rw_interrupt) {
		u.u_error = EINTR;
		__builtin_longjmp((void **)&u.u_qsave, 1);
	}
	return rw_error;
}

static int
stub_ioctl(struct file *f, u_int com, char *data)
{
	(void)f; (void)com; (void)data;
	return ENOTTY;
}

static int
stub_select(struct file *f, int flag)
{
	(void)f; (void)flag;
	return 0;
}

static int
stub_close(struct file *f)
{
	(void)f;
	return 0;
}

/* sys_generic.c defines Fops over these two, indexed by f_type. */
const struct fileops inodeops = { stub_rw, stub_ioctl, stub_select, stub_close };
const struct fileops pipeops = { stub_rw, stub_ioctl, stub_select, stub_close };

/* Injected failure for the copies, which cannot fail on a host. */
static int copy_fails;

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	if (copy_fails)
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	if (copy_fails)
		return EFAULT;
	bcopy(from, to, nbytes);
	return 0;
}

/* The rest of what sys_generic.c names, none of it reached by rwuio. */
struct file *getf(int f) { (void)f; return NULL; }
int fset(struct file *f, int bit, int value) { (void)f; (void)bit; (void)value; return 0; }
int fgetown(struct file *f, int *valuep) { (void)f; (void)valuep; return 0; }
int fsetown(struct file *f, int value) { (void)f; (void)value; return 0; }
int tsleep(caddr_t ident, int priority, u_int timo)
{ (void)ident; (void)priority; (void)timo; return 0; }
void unsleep(struct proc *p) { (void)p; }
void setrun(struct proc *p) { (void)p; }
void wakeup(caddr_t chan) { (void)chan; }
int hzto(struct timeval *tv) { (void)tv; return 0; }
void timevaladd(struct timeval *t1, struct timeval *t2) { (void)t1; (void)t2; }
int itimerfix(struct timeval *tv) { (void)tv; return 0; }
int baduaddr(caddr_t addr) { (void)addr; return 0; }
int ffs(u_long i) { int n = 0; if (i == 0) return 0; while (!(i & 1)) { i >>= 1; n++; } return n + 1; }

/* The argument shapes the kernel casts u.u_arg to. */
struct rw_arg {
	int fdes;
	char *cbuf;
	unsigned count;
};

struct rwv_arg {
	int fdes;
	struct iovec *iovp;
	unsigned iovcnt;
};

static struct iovec vectors[MAXVEC + 1];
static char buffer[64];

static void
reset(int flags)
{
	bzero(&u, sizeof u);
	bzero(&fp, sizeof fp);
	fp.f_flag = flags;
	fp.f_type = DTYPE_INODE;
	fp.f_count = 1;
	u.u_ofile[FD] = &fp;
	rw_calls = 0;
	rw_resid_seen = 0;
	rw_consume = LIMIT;
	rw_error = 0;
	rw_interrupt = 0;
	copy_fails = 0;
	hk_reset_output();
}

static void
vectorize(const u_int *lengths, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		vectors[i].iov_base = buffer;
		vectors[i].iov_len = lengths[i];
	}
}

static void
call_readv(int fd, unsigned iovcnt)
{
	struct rwv_arg *a = (struct rwv_arg *)u.u_arg;

	a->fdes = fd;
	a->iovp = vectors;
	a->iovcnt = iovcnt;
	readv();
}

static void
call_writev(int fd, unsigned iovcnt)
{
	struct rwv_arg *a = (struct rwv_arg *)u.u_arg;

	a->fdes = fd;
	a->iovp = vectors;
	a->iovcnt = iovcnt;
	writev();
}

static void
call_read(int fd, unsigned count)
{
	struct rw_arg *a = (struct rw_arg *)u.u_arg;

	a->fdes = fd;
	a->cbuf = buffer;
	a->count = count;
	read();
}

static void
call_write(int fd, unsigned count)
{
	struct rw_arg *a = (struct rw_arg *)u.u_arg;

	a->fdes = fd;
	a->cbuf = buffer;
	a->count = count;
	write();
}

/* readv over the given lengths; true when the kernel accepted the request. */
static int
accepted(const u_int *lengths, unsigned n)
{
	reset(FREAD);
	vectorize(lengths, n);
	call_readv(FD, n);
	return u.u_error == 0;
}

/* A request the kernel rejected reaches no file operation and moves nothing. */
static void
check_rejected(const char *label, const u_int *lengths, unsigned n)
{
	if (accepted(lengths, n))
		hk_fail(__FILE__, __LINE__, label);
	hk_checks++;
	HK_CHECK(u.u_error == EINVAL);
	HK_CHECK(rw_calls == 0);
	HK_CHECK(fp.f_offset == 0);
	HK_CHECK(u.u_rval == 0);
}

static void
check_accepted(const char *label, const u_int *lengths, unsigned n, u_int total)
{
	if (!accepted(lengths, n))
		hk_fail(__FILE__, __LINE__, label);
	hk_checks++;
	HK_CHECK(rw_calls == 1);
	HK_CHECK(rw_resid_seen == total);
	HK_CHECK((u_int)u.u_rval == total);
	HK_CHECK((u_int)fp.f_offset == total);
}

static void
bound_cases(void)
{
	static const u_int at_limit[] = { LIMIT };
	static const u_int below[] = { LIMIT - 1 };
	static const u_int above[] = { LIMIT + 1 };
	static const u_int single_oversized[] = { 0x80000000U };
	static const u_int spread[] = { 0x40000000U, 0x40000000U, 1 };
	static const u_int spread_edge[] = { 0x40000000U, 0x3fffffffU };
	static const u_int wrap_to_zero[] = { 0xffffffffU, 1 };
	static const u_int wrap_to_one[] = { 0xffffffffU, 2 };
	static const u_int wrap_late[] = { 1, 1, 1, 0xfffffffdU };

	check_accepted("at the limit", at_limit, 1, LIMIT);
	check_accepted("one below the limit", below, 1, LIMIT - 1);
	check_rejected("one above the limit", above, 1);
	check_rejected("a single oversized vector", single_oversized, 1);
	check_rejected("overflow spread across three vectors", spread, 3);
	check_accepted("the limit spread across two vectors", spread_edge, 2, LIMIT);
	check_rejected("0xffffffff + 1, which wrapped to a request of 0", wrap_to_zero, 2);
	check_rejected("0xffffffff + 2, which wrapped to a request of 1", wrap_to_one, 2);
	check_rejected("overflow in the last of four vectors", wrap_late, 4);
}

/* Splitting an accepted length across vectors changes nothing the kernel reports. */
static void
split_cases(void)
{
	static const u_int whole[] = { 6 };
	static const u_int thirds[] = { 1, 2, 3 };
	static const u_int halves[] = { 3, 3 };
	static const u_int with_empty[] = { 0, 6, 0 };
	static const u_int limit_whole[] = { LIMIT };
	static const u_int limit_split[] = { 0x3fffffffU, 0x3fffffffU, 1 };

	check_accepted("6 in one vector", whole, 1, 6);
	check_accepted("6 in three vectors", thirds, 3, 6);
	check_accepted("6 in two vectors", halves, 2, 6);
	check_accepted("6 with empty vectors around it", with_empty, 3, 6);
	check_accepted("the limit in one vector", limit_whole, 1, LIMIT);
	check_accepted("the limit in three vectors", limit_split, 3, LIMIT);
}

/*
 * The kernel's verdict against an unbounded sum: over every pair and
 * triple drawn from a set of lengths around the powers of two and the
 * limit, the request is accepted exactly when the 64-bit sum is at most
 * the limit.
 */
static void
reference_cases(void)
{
	static const u_int pool[] = {
		0, 1, 2, 0x3fffffffU, 0x40000000U, 0x7ffffffeU, 0x7fffffffU,
		0x80000000U, 0xfffffffeU, 0xffffffffU,
	};
	const unsigned n = sizeof pool / sizeof pool[0];
	unsigned i, j, k, agreed = 0, cases = 0;
	u_int lengths[3];
	unsigned long long sum;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			lengths[0] = pool[i];
			lengths[1] = pool[j];
			sum = (unsigned long long)pool[i] + pool[j];
			cases++;
			if (accepted(lengths, 2) == (sum <= LIMIT))
				agreed++;
			for (k = 0; k < n; k++) {
				lengths[2] = pool[k];
				cases++;
				if (accepted(lengths, 3) == (sum + pool[k] <= LIMIT))
					agreed++;
			}
		}
	HK_CHECK(agreed == cases);
	hk_note("reference: %u of %u vector sets agree with the 64-bit sum", agreed, cases);
}

/* read(2) and write(2) take one count and meet the same bound. */
static void
single_count_cases(void)
{
	reset(FREAD | FWRITE);
	call_read(FD, LIMIT);
	HK_CHECK(u.u_error == 0 && rw_calls == 1 && rw_resid_seen == LIMIT);

	reset(FREAD | FWRITE);
	call_read(FD, LIMIT + 1);
	HK_CHECK(u.u_error == EINVAL && rw_calls == 0 && fp.f_offset == 0);

	reset(FREAD | FWRITE);
	call_write(FD, 0xffffffffU);
	HK_CHECK(u.u_error == EINVAL && rw_calls == 0 && fp.f_offset == 0);

	reset(FREAD | FWRITE);
	call_write(FD, 5);
	HK_CHECK(u.u_error == 0 && rw_calls == 1 && rw_direction == UIO_WRITE);
	HK_CHECK(u.u_rval == 5 && fp.f_offset == 5);
}

/* The descriptor, the count and the copy, before any length is summed. */
static void
entry_cases(void)
{
	static const u_int three[] = { 1, 2, 3 };
	static const u_int many[MAXVEC + 1] = { 1 };

	/* A closed descriptor and one open the other way. */
	reset(FREAD);
	vectorize(three, 3);
	call_readv(FD + 1, 3);
	HK_CHECK(u.u_error == EBADF && rw_calls == 0);
	reset(FWRITE);
	vectorize(three, 3);
	call_readv(FD, 3);
	HK_CHECK(u.u_error == EBADF && rw_calls == 0);
	reset(FREAD);
	vectorize(three, 3);
	call_writev(FD, 3);
	HK_CHECK(u.u_error == EBADF && rw_calls == 0);

	/* More vectors than the kernel's local array: refused before the copy. */
	reset(FREAD);
	vectorize(many, MAXVEC + 1);
	copy_fails = 1;
	call_readv(FD, MAXVEC + 1);
	HK_CHECK(u.u_error == EINVAL && rw_calls == 0);

	/* Exactly the array: accepted, all sixteen summed. */
	reset(FREAD);
	vectorize(many, MAXVEC);
	call_readv(FD, MAXVEC);
	HK_CHECK(u.u_error == 0 && rw_calls == 1 && rw_resid_seen == 1);

	/* No vectors: a request of zero bytes, which reaches the file. */
	reset(FREAD);
	call_readv(FD, 0);
	HK_CHECK(u.u_error == 0 && rw_calls == 1 && rw_resid_seen == 0 && u.u_rval == 0);

	/* The vector copy fails: EFAULT, and nothing is summed or reached. */
	reset(FREAD);
	vectorize(three, 3);
	copy_fails = 1;
	call_readv(FD, 3);
	HK_CHECK(u.u_error == EFAULT && rw_calls == 0);
}

/* What the file operation transfers is what the caller is told. */
static void
transfer_cases(void)
{
	static const u_int three[] = { 1, 2, 3 };

	/* A short transfer: the count is the bytes moved. */
	reset(FREAD);
	vectorize(three, 3);
	rw_consume = 4;
	call_readv(FD, 3);
	HK_CHECK(u.u_error == 0 && u.u_rval == 4 && fp.f_offset == 4);

	/* The file operation fails: its error, and rval is the bytes moved. */
	reset(FREAD);
	vectorize(three, 3);
	rw_consume = 0;
	rw_error = EIO;
	call_readv(FD, 3);
	HK_CHECK(u.u_error == EIO && u.u_rval == 0);

	/* Interrupted before any byte moved: EINTR stands, for a restart. */
	reset(FREAD);
	vectorize(three, 3);
	rw_consume = 0;
	rw_interrupt = 1;
	call_readv(FD, 3);
	HK_CHECK(u.u_error == EINTR && u.u_rval == 0 && rw_calls == 1);

	/* Interrupted after two bytes: the error is dropped and 2 is returned. */
	reset(FREAD);
	vectorize(three, 3);
	rw_consume = 2;
	rw_interrupt = 1;
	call_readv(FD, 3);
	HK_CHECK(u.u_error == 0 && u.u_rval == 2 && fp.f_offset == 2);

	/* writev takes the same path with the other flag. */
	reset(FWRITE);
	vectorize(three, 3);
	call_writev(FD, 3);
	HK_CHECK(u.u_error == 0 && u.u_rval == 6 && rw_direction == UIO_WRITE);
}

int
main(void)
{
	hk_note("widths: size_t %u off_t %u u_int %u long %u",
	    (unsigned)sizeof(size_t), (unsigned)sizeof(off_t),
	    (unsigned)sizeof(u_int), (unsigned)sizeof(long));
	bound_cases();
	split_cases();
	reference_cases();
	single_count_cases();
	entry_cases();
	transfer_cases();
	return hk_verdict("rwuio");
}
