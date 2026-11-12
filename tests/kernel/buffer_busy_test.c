/*
 * Host gate for buffer-cache pressure in sys/kern/ufs_bio.c.
 *
 * The gate compiles the production buffer cache unmodified at the target's
 * width in the rp2040 configuration (NBUF 4, linear cache) and links it with
 * the production sys/kern/subr_capacity.c. getnewbuf() separates two states
 * that both leave no buffer free for a new block:
 *
 *   - every buffer is B_BUSY: the caller counts one buffer capacity failure
 *     and one buffer wait, sets B_WANTED on the free-list head bfreelist[0]
 *     and sleeps there until brelse() wakes it;
 *   - a free buffer holds a delayed write: getnewbuf() counts an eviction
 *     write and writes the block with bawrite(), which raises no capacity
 *     failure and does not sleep.
 *
 * The seam is sleep() and wakeup(). sleep() here records the channel and the
 * counters at the instant of the call, then runs a scripted action standing
 * in for the process that would run while the caller is blocked, normally
 * brelse() of a held buffer. A sleep() whose script released nothing, or
 * whose release delivered no wakeup on the channel, leaves the caller
 * blocked for good, so it leaves through hk_escape() and the caller never
 * completes. The flow stays single-threaded and deterministic.
 *
 * The disk is RAM. Its strategy routine records the block number and bytes
 * of every write, completes the transfer at once with biodone() and serves
 * reads from the same store.
 *
 * Storage counters come from a counting stand-in for storage_note(), whose
 * production definition lives in the flash driver and cannot link here. The
 * buffer capacity counters are the production ones.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/file.h>
#include <sys/buf.h>
#include <sys/capacity.h>
#include <sys/clist.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/storage_stats.h>

_Static_assert(sizeof(void *) == 4, "the target is ILP32");
_Static_assert(NBUF == 4, "the rp2040 configuration holds four buffers");

struct proc proc[NPROC];
struct inode inode[NINODE];
struct file file[NFILE];
struct mount mount[NMOUNT];
struct buf buf[NBUF];
struct buf bfreelist[BQUEUES];
char bufdata[NBUF * MAXBSIZE];
int cfreecount;

#define RAMDISK_MAJOR 1
#define DEV makedev(RAMDISK_MAJOR, 0)
#define DISK_BLOCKS 32
#define WRITE_LOG 64
#define FIRST_BLOCK 10
#define SLEEP_LIMIT 4

/* The modeled device. */
static unsigned char disk[DISK_BLOCKS][DEV_BSIZE];
static unsigned disk_write_count[DISK_BLOCKS];
static daddr_t write_log[WRITE_LOG];
static unsigned device_writes;
static unsigned device_reads;

static void
ramdisk_strategy(struct buf *bp)
{
	unsigned block = (unsigned)bp->b_blkno;

	if (block >= DISK_BLOCKS) {
		hk_fail(__FILE__, __LINE__, "strategy block out of range");
		bp->b_flags |= B_ERROR;
		bp->b_error = EINVAL;
		biodone(bp);
		return;
	}
	if (bp->b_flags & B_READ) {
		device_reads++;
		bcopy(disk[block], bp->b_addr, DEV_BSIZE);
	} else {
		if (device_writes < WRITE_LOG)
			write_log[device_writes] = bp->b_blkno;
		device_writes++;
		disk_write_count[block]++;
		bcopy(bp->b_addr, disk[block], DEV_BSIZE);
	}
	bp->b_resid = 0;
	biodone(bp);
}

const struct bdevsw bdevsw[] = {
	[0] = { 0, 0, 0, 0, 0, 0 },
	[RAMDISK_MAJOR] = { 0, 0, ramdisk_strategy, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
};
const int nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]) - 1;

/* Counting stand-ins for the flash driver's storage counters. */
static uint32_t storage_counts[STORAGE_METRIC_COUNT];
static unsigned dirty_samples;

void
storage_note(enum storage_metric metric, uint32_t amount)
{
	storage_counts[metric] += amount;
}

void
storage_dirty_sample(void)
{
	dirty_samples++;
}

/* The sleep and wakeup seam. */
struct sleep_record {
	caddr_t chan;
	int pri;
	int freelist_wanted;
	uint32_t failures;
	uint32_t waits;
	uint32_t evictions;
	unsigned device_writes;
};

/*
 * What runs while the caller is blocked: release a held buffer with
 * brelse(), and wake the free-list channel with no buffer released (a
 * spurious wakeup). An all-zero entry runs nothing.
 */
struct sleep_action {
	struct buf *release;
	int spurious;
};

static struct sleep_record sleeps[SLEEP_LIMIT + 1];
static unsigned sleep_calls;
static struct sleep_action script[SLEEP_LIMIT];
static unsigned freelist_wakeups;
static unsigned buffer_wakeups;
static unsigned lost_wakeups;

static uint32_t
cap_failures(void)
{
	return capacity_snapshot()->metric[CAPACITY_BUFFER].failures;
}

static uint16_t
cap_current(void)
{
	return capacity_snapshot()->metric[CAPACITY_BUFFER].current;
}

static uint16_t
cap_peak(void)
{
	return capacity_snapshot()->metric[CAPACITY_BUFFER].peak;
}

void
wakeup(caddr_t chan)
{
	if (chan == (caddr_t)bfreelist)
		freelist_wakeups++;
	else
		buffer_wakeups++;
}

void
sleep(caddr_t chan, int pri)
{
	struct sleep_record *record;
	struct sleep_action action;
	unsigned wakeups_before = freelist_wakeups;

	if (sleep_calls >= SLEEP_LIMIT) {
		sleep_calls++;
		hk_escape();
	}
	record = &sleeps[sleep_calls];
	record->chan = chan;
	record->pri = pri;
	record->freelist_wanted = (bfreelist[0].b_flags & B_WANTED) != 0;
	record->failures = cap_failures();
	record->waits = storage_counts[STORAGE_BUFFER_WAITS];
	record->evictions = storage_counts[STORAGE_EVICTION_WRITES];
	record->device_writes = device_writes;
	action = script[sleep_calls++];
	if (action.release != NULL)
		brelse(action.release);
	if (action.spurious)
		wakeup((caddr_t)bfreelist);
	/*
	 * A caller sleeping on the free-list head resumes only through a
	 * wakeup on that channel. Without one the process stays blocked, which
	 * the host models by abandoning the call.
	 */
	if (freelist_wakeups == wakeups_before) {
		lost_wakeups++;
		hk_escape();
	}
}

static void
check_named(int line, const char *name, int ok)
{
	hk_checks++;
	if (ok) {
		hk_note("PASS %s", name);
		return;
	}
	hk_note("FAIL %s", name);
	hk_fail(__FILE__, line, name);
}

#define CHECK(name, cond) check_named(__LINE__, name, (cond) != 0)

static unsigned char
pattern_byte(unsigned block, unsigned offset)
{
	return (unsigned char)(block * 37U + offset * 7U + 0x5aU);
}

static void
fill_pattern(struct buf *bp, unsigned block)
{
	unsigned offset;

	for (offset = 0; offset < DEV_BSIZE; offset++)
		bp->b_addr[offset] = (char)pattern_byte(block, offset);
}

static int
disk_holds_pattern(unsigned block)
{
	unsigned offset;

	for (offset = 0; offset < DEV_BSIZE; offset++)
		if (disk[block][offset] != pattern_byte(block, offset))
			return 0;
	return 1;
}

/* Rebuild the cache as init_main.c's binit() leaves it. */
static void
reset_machine(void)
{
	struct buf *bp;
	unsigned i;

	bzero(buf, sizeof(buf));
	bzero(bfreelist, sizeof(bfreelist));
	bzero(bufdata, sizeof(bufdata));
	bzero(disk, sizeof(disk));
	bzero(disk_write_count, sizeof(disk_write_count));
	bzero(write_log, sizeof(write_log));
	bzero(sleeps, sizeof(sleeps));
	bzero(script, sizeof(script));
	device_writes = device_reads = 0;
	sleep_calls = freelist_wakeups = buffer_wakeups = lost_wakeups = 0;
	hk_ipl = 0;
	for (bp = bfreelist; bp < &bfreelist[BQUEUES]; bp++)
		bp->av_forw = bp->av_back = bp;
	for (i = 0; i < NBUF; i++) {
		bp = &buf[i];
		bp->b_dev = NODEV;
		bp->b_bcount = 0;
		bp->b_addr = bufdata + MAXBSIZE * i;
		bp->b_flags = B_BUSY | B_INVAL;
		brelse(bp);
	}
}

/* getblk() under hk_catch(): an abandoned sleep leaves the result NULL. */
static daddr_t request_block;
static struct buf *request_result;

static void
run_getblk(void)
{
	request_result = getblk(DEV, request_block);
}

static struct buf *
try_getblk(daddr_t block)
{
	request_block = block;
	request_result = NULL;
	if (hk_catch(run_getblk))
		request_result = NULL;
	hk_ipl = 0;
	return request_result;
}

static int
all_distinct(struct buf *const *bufs, unsigned count)
{
	unsigned i, j;

	for (i = 0; i < count; i++)
		for (j = i + 1; j < count; j++)
			if (bufs[i] == bufs[j])
				return 0;
	return 1;
}

static int
all_busy(struct buf *const *bufs, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
		if (bufs[i] == NULL || (bufs[i]->b_flags & B_BUSY) == 0)
			return 0;
	return 1;
}

static int
log_is_pattern_exact(void)
{
	unsigned i;

	for (i = 0; i < device_writes && i < WRITE_LOG; i++)
		if (!disk_holds_pattern((unsigned)write_log[i]))
			return 0;
	return 1;
}

/*
 * Scenario 1: every buffer busy. The fifth block waits on the free-list head
 * until a held buffer is released.
 */
static void
scenario_all_busy(void)
{
	struct buf *held[NBUF];
	struct buf *waiter;
	uint32_t failures, waits, evictions;
	unsigned i;

	hk_note("scenario all-busy");
	reset_machine();
	failures = cap_failures();
	waits = storage_counts[STORAGE_BUFFER_WAITS];
	evictions = storage_counts[STORAGE_EVICTION_WRITES];
	for (i = 0; i < NBUF; i++)
		held[i] = getblk(DEV, FIRST_BLOCK + (daddr_t)i);

	CHECK("all-busy/four-distinct-busy-buffers",
	    all_busy(held, NBUF) && all_distinct(held, NBUF));
	CHECK("all-busy/no-pressure-while-a-buffer-is-free",
	    sleep_calls == 0 && cap_failures() == failures &&
	    storage_counts[STORAGE_BUFFER_WAITS] == waits);
	CHECK("all-busy/occupancy-reaches-limit",
	    cap_current() == NBUF && cap_peak() == NBUF);

	script[0].release = held[0];
	waiter = try_getblk(FIRST_BLOCK + NBUF);

	CHECK("all-busy/getblk-sleeps-once", sleep_calls == 1);
	CHECK("all-busy/sleep-channel-is-free-list-head",
	    sleeps[0].chan == (caddr_t)&bfreelist[0]);
	CHECK("all-busy/sleep-priority-is-pribio-plus-one",
	    sleeps[0].pri == PRIBIO + 1);
	CHECK("all-busy/b-wanted-set-on-free-list-head-at-sleep",
	    sleeps[0].freelist_wanted);
	CHECK("all-busy/capacity-failure-raised-by-one-before-sleep",
	    sleeps[0].failures == failures + 1);
	CHECK("all-busy/buffer-wait-counted-before-sleep",
	    sleeps[0].waits == waits + 1);
	CHECK("all-busy/no-eviction-write-while-all-busy",
	    storage_counts[STORAGE_EVICTION_WRITES] == evictions &&
	    device_writes == 0);
	CHECK("all-busy/release-wakes-free-list-waiter",
	    freelist_wakeups == 1 && lost_wakeups == 0);
	CHECK("all-busy/b-wanted-cleared-by-release",
	    (bfreelist[0].b_flags & B_WANTED) == 0);
	CHECK("all-busy/waiter-completes-with-released-buffer",
	    waiter == held[0]);
	CHECK("all-busy/waiter-buffer-is-busy-for-requested-block",
	    waiter != NULL && (waiter->b_flags & B_BUSY) != 0 &&
	    waiter->b_dev == DEV &&
	    waiter->b_blkno == fsbtodb(FIRST_BLOCK + NBUF));
	CHECK("all-busy/exactly-one-failure-and-one-wait-total",
	    cap_failures() == failures + 1 &&
	    storage_counts[STORAGE_BUFFER_WAITS] == waits + 1);
	CHECK("all-busy/occupancy-stays-at-limit-after-handoff",
	    cap_current() == NBUF);
}

/*
 * Scenario 1, repeated: a wakeup that frees no buffer sends the caller back
 * to sleep, and each wait counts its own failure.
 */
static void
scenario_repeated_wait(void)
{
	struct buf *held[NBUF];
	struct buf *waiter;
	uint32_t failures, waits;
	unsigned i;

	hk_note("scenario all-busy-repeated-wait");
	reset_machine();
	for (i = 0; i < NBUF; i++)
		held[i] = getblk(DEV, FIRST_BLOCK + (daddr_t)i);
	failures = cap_failures();
	waits = storage_counts[STORAGE_BUFFER_WAITS];

	/*
	 * The first wakeup frees no buffer, so the waiter finds every buffer
	 * busy again and sleeps a second time; the second wakeup releases one.
	 */
	script[0].spurious = 1;
	script[1].release = held[2];
	waiter = try_getblk(FIRST_BLOCK + NBUF);

	CHECK("repeated-wait/getblk-sleeps-twice", sleep_calls == 2);
	CHECK("repeated-wait/each-sleep-follows-its-own-failure",
	    sleeps[0].failures == failures + 1 &&
	    sleeps[1].failures == failures + 2);
	CHECK("repeated-wait/each-sleep-follows-its-own-wait",
	    sleeps[0].waits == waits + 1 && sleeps[1].waits == waits + 2);
	CHECK("repeated-wait/both-sleeps-use-the-free-list-head",
	    sleeps[0].chan == (caddr_t)&bfreelist[0] &&
	    sleeps[1].chan == (caddr_t)&bfreelist[0] &&
	    sleeps[0].freelist_wanted && sleeps[1].freelist_wanted);
	CHECK("repeated-wait/waiter-completes-with-the-released-buffer",
	    waiter == held[2] && lost_wakeups == 0 &&
	    (waiter->b_flags & B_BUSY) != 0);
	CHECK("repeated-wait/failures-and-waits-total-two",
	    cap_failures() == failures + 2 &&
	    storage_counts[STORAGE_BUFFER_WAITS] == waits + 2);
}

/*
 * Scenario 2: all four buffers hold delayed writes and none is busy. The
 * fifth block writes dirty data out and never waits.
 */
static void
scenario_all_delayed(void)
{
	struct buf *bufs[NBUF];
	struct buf *result;
	uint32_t failures, waits, evictions;
	unsigned i, delayed = 0, busy = 0;

	hk_note("scenario all-delayed-write");
	reset_machine();
	failures = cap_failures();
	waits = storage_counts[STORAGE_BUFFER_WAITS];
	evictions = storage_counts[STORAGE_EVICTION_WRITES];
	for (i = 0; i < NBUF; i++) {
		bufs[i] = getblk(DEV, FIRST_BLOCK + (daddr_t)i);
		fill_pattern(bufs[i], FIRST_BLOCK + i);
		bdwrite(bufs[i]);
	}
	for (i = 0; i < NBUF; i++) {
		delayed += (bufs[i]->b_flags & B_DELWRI) != 0;
		busy += (bufs[i]->b_flags & B_BUSY) != 0;
	}
	CHECK("all-delayed/four-delayed-writes-none-busy",
	    delayed == NBUF && busy == 0 && device_writes == 0);
	CHECK("all-delayed/occupancy-counts-busy-only", cap_current() == 0);

	result = try_getblk(FIRST_BLOCK + NBUF);
	hk_note("  evictions=%u device_writes=%u",
	    (unsigned)(storage_counts[STORAGE_EVICTION_WRITES] - evictions),
	    device_writes);

	CHECK("all-delayed/getblk-does-not-sleep",
	    sleep_calls == 0 && lost_wakeups == 0 && result != NULL);
	CHECK("all-delayed/no-capacity-failure",
	    cap_failures() == failures);
	CHECK("all-delayed/no-buffer-wait",
	    storage_counts[STORAGE_BUFFER_WAITS] == waits);
	CHECK("all-delayed/eviction-write-counted",
	    storage_counts[STORAGE_EVICTION_WRITES] > evictions);
	CHECK("all-delayed/every-eviction-reaches-the-device",
	    storage_counts[STORAGE_EVICTION_WRITES] - evictions ==
	    device_writes && device_writes > 0);
	CHECK("all-delayed/oldest-block-written-first",
	    device_writes > 0 && write_log[0] == fsbtodb(FIRST_BLOCK));
	CHECK("all-delayed/evicted-bytes-match-byte-for-byte",
	    disk_holds_pattern(FIRST_BLOCK) && log_is_pattern_exact());
	CHECK("all-delayed/new-block-buffer-is-busy",
	    result != NULL && (result->b_flags & B_BUSY) != 0 &&
	    (result->b_flags & B_DELWRI) == 0 &&
	    result->b_blkno == fsbtodb(FIRST_BLOCK + NBUF));
}

/*
 * Scenario 3: three buffers busy, one holds a delayed write. The delayed
 * buffer is the only candidate; it is written and reused with no wait.
 */
static void
scenario_mixed(void)
{
	struct buf *bufs[NBUF];
	struct buf *result;
	uint32_t failures, waits, evictions;
	unsigned i;

	hk_note("scenario mixed");
	reset_machine();
	failures = cap_failures();
	waits = storage_counts[STORAGE_BUFFER_WAITS];
	evictions = storage_counts[STORAGE_EVICTION_WRITES];
	for (i = 0; i < NBUF; i++)
		bufs[i] = getblk(DEV, FIRST_BLOCK + (daddr_t)i);
	fill_pattern(bufs[NBUF - 1], FIRST_BLOCK + NBUF - 1);
	bdwrite(bufs[NBUF - 1]);
	CHECK("mixed/three-busy-one-delayed",
	    all_busy(bufs, NBUF - 1) &&
	    (bufs[NBUF - 1]->b_flags & (B_BUSY | B_DELWRI)) == B_DELWRI &&
	    cap_current() == NBUF - 1);

	result = try_getblk(FIRST_BLOCK + NBUF);

	CHECK("mixed/getblk-does-not-sleep",
	    sleep_calls == 0 && lost_wakeups == 0 && result != NULL);
	CHECK("mixed/no-capacity-failure", cap_failures() == failures);
	CHECK("mixed/no-buffer-wait",
	    storage_counts[STORAGE_BUFFER_WAITS] == waits);
	CHECK("mixed/one-eviction-write-counted",
	    storage_counts[STORAGE_EVICTION_WRITES] == evictions + 1);
	CHECK("mixed/delayed-write-reaches-device",
	    device_writes == 1 && write_log[0] == fsbtodb(FIRST_BLOCK + NBUF - 1) &&
	    disk_holds_pattern(FIRST_BLOCK + NBUF - 1));
	CHECK("mixed/result-reuses-the-delayed-buffer",
	    result == bufs[NBUF - 1] && (result->b_flags & B_BUSY) != 0 &&
	    result->b_blkno == fsbtodb(FIRST_BLOCK + NBUF));
	CHECK("mixed/held-buffers-stay-busy",
	    all_busy(bufs, NBUF - 1));
}

/*
 * Scenario 4: delayed data survives pressure. The dirty buffer is held busy
 * with three others, a waiter sleeps and a clean buffer is released; after
 * every buffer is released, bflush() writes the pattern and the occupancy
 * returns to zero while the peak and failure counts stay latched.
 */
static void
scenario_recovery(void)
{
	enum { DIRTY_BLOCK = 20 };
	struct buf *dirty, *hit, *held[3], *waiter;
	struct buf *all[NBUF];
	uint32_t failures, waits;
	uint16_t peak;
	unsigned i, still_delayed = 0;

	hk_note("scenario recovery-under-pressure");
	reset_machine();
	dirty = getblk(DEV, DIRTY_BLOCK);
	fill_pattern(dirty, DIRTY_BLOCK);
	bdwrite(dirty);
	for (i = 0; i < 3; i++)
		held[i] = getblk(DEV, DIRTY_BLOCK + 1 + (daddr_t)i);
	CHECK("recovery/dirty-buffer-stays-free-while-clean-ones-are-taken",
	    (dirty->b_flags & (B_BUSY | B_DELWRI)) == B_DELWRI &&
	    device_writes == 0);
	hit = getblk(DEV, DIRTY_BLOCK);
	CHECK("recovery/cache-hit-returns-the-dirty-buffer-busy",
	    hit == dirty && (hit->b_flags & B_BUSY) != 0 &&
	    (hit->b_flags & B_DELWRI) != 0);
	failures = cap_failures();
	waits = storage_counts[STORAGE_BUFFER_WAITS];
	CHECK("recovery/all-buffers-busy", cap_current() == NBUF);

	script[0].release = held[0];
	waiter = try_getblk(DIRTY_BLOCK + 4);

	CHECK("recovery/waiter-sleeps-on-free-list-head-and-completes",
	    sleep_calls == 1 && sleeps[0].chan == (caddr_t)&bfreelist[0] &&
	    sleeps[0].freelist_wanted && waiter == held[0]);
	CHECK("recovery/one-failure-and-one-wait-for-the-sleep",
	    cap_failures() == failures + 1 &&
	    storage_counts[STORAGE_BUFFER_WAITS] == waits + 1);
	CHECK("recovery/no-device-write-while-dirty-buffer-is-busy",
	    device_writes == 0);

	peak = cap_peak();
	all[0] = hit;
	all[1] = held[1];
	all[2] = held[2];
	all[3] = waiter;
	for (i = 0; i < NBUF; i++)
		if (all[i] != NULL)
			brelse(all[i]);
	CHECK("recovery/occupancy-returns-to-zero-after-release",
	    cap_current() == 0);
	CHECK("recovery/peak-stays-latched",
	    cap_peak() == peak && peak == NBUF);
	CHECK("recovery/failure-count-stays-latched",
	    cap_failures() == failures + 1);

	CHECK("recovery/bflush-succeeds", bflush(DEV) == 0);
	CHECK("recovery/pattern-reaches-the-device",
	    disk_holds_pattern(DIRTY_BLOCK) &&
	    disk_write_count[DIRTY_BLOCK] == 1 && device_writes == 1);
	for (i = 0; i < NBUF; i++)
		still_delayed += (buf[i].b_flags & B_DELWRI) != 0;
	CHECK("recovery/no-delayed-write-remains", still_delayed == 0);
	CHECK("recovery/flush-leaves-failure-and-peak-latched",
	    cap_failures() == failures + 1 && cap_peak() == peak &&
	    cap_current() == 0);
}

int
main(void)
{
	int status;

	cfreecount = NCLIST * CBSIZE;
	capacity_init();
	scenario_all_busy();
	scenario_repeated_wait();
	scenario_all_delayed();
	scenario_mixed();
	scenario_recovery();
	hk_note("  buffers=%d dirty_samples=%u", NBUF, dirty_samples);
	status = hk_verdict("buffer busy");
	if (status == 0)
		hk_note("BUFFER BUSY TEST OK");
	return status;
}
