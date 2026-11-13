/*
 * Host gate for the temporary-swap ioctl provider in sys/dev/swap.c.
 *
 * The generated companion contains the exact production state, swsize() and
 * swcioctl() definitions.  The gate links the production resource-map
 * allocator so replacement, shortage, erase-aligned release and cursor
 * publication are exercised together.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/disk.h>
#include <sys/user.h>

#define MAP_SLOTS 8
#define MAP_START 4
#define MAP_BLOCKS 60
#define APPEND_DISABLED 0xffffU

#ifdef HK_ILP32
_Static_assert(sizeof(off_t) == 4, "RP2040 off_t must occupy four bytes");
_Static_assert(sizeof(int) == 4, "RP2040 int must occupy four bytes");
_Static_assert(TFALLOC == 0xc0047301UL, "TFALLOC ABI value changed");
_Static_assert(DIOCGETMEDIASIZE == 0x40046401UL,
    "DIOCGETMEDIASIZE ABI value changed");
#endif

static struct mapent swap_entries[MAP_SLOTS + 1];
static char swapmap_name[] = "swapmap";
struct map swapmap[1] = {
    { swap_entries, &swap_entries[MAP_SLOTS - 1], swapmap_name, 0, 0, 0 }
};
size_t swapnext;
u_int nswap;
dev_t swapdev;
int securelevel;

/*
 * The live swap device behind minor 64, whose open and strategy routines
 * count the requests swopen() and swstrategy() pass through, and the
 * temporary-unit transfer swap() performs.
 */
static unsigned live_opens, live_transfers, temp_transfers, completions;

static int
live_open(dev_t dev, int mode, int flag)
{
    (void)dev;
    (void)mode;
    (void)flag;
    live_opens++;
    return 0;
}

static void
live_strategy(struct buf *bp)
{
    (void)bp;
    live_transfers++;
}

const struct bdevsw bdevsw[5] = {
    { .d_open = live_open, .d_strategy = live_strategy },
};

void
swap(size_t blkno, size_t coreaddr, int count, int rdflg)
{
    (void)blkno;
    (void)coreaddr;
    (void)count;
    (void)rdflg;
    temp_transfers++;
}

void
biodone(struct buf *bp)
{
    bp->b_flags |= B_DONE;
    completions++;
}

struct user u;
static int allow_superuser;
static unsigned superuser_calls;
static unsigned cursor_publications;
static size_t published_cursor;

extern void swap_ioctl_test_reset(void);
extern u_int swap_ioctl_test_start(int unit);
extern u_int swap_ioctl_test_size(int unit);
extern u_int swap_ioctl_test_next(int unit);

/* suser() as sys/kern/kern_prot2.c defines it, with the answer armed. */
int
suser(void)
{
    superuser_calls++;
    if (!allow_superuser)
        u.u_error = EPERM;
    return allow_superuser;
}

void
swap_cursor_publish(size_t next)
{
    cursor_publications++;
    published_cursor = next;
}

static void
reset_fixture(void)
{
    unsigned index;

    for (index = 0; index < MAP_SLOTS + 1; index++) {
        swap_entries[index].m_size = 0;
        swap_entries[index].m_addr = 0;
    }
    swapmap[0].m_map = swap_entries;
    swapmap[0].m_limit = &swap_entries[MAP_SLOTS - 1];
    swapmap[0].m_name = swapmap_name;
    mfree(swapmap, MAP_BLOCKS, MAP_START);
    swapnext = MAP_START;
    nswap = MAP_START + MAP_BLOCKS;
    cursor_publications = 0;
    published_cursor = 0;
    allow_superuser = 1;
    superuser_calls = 0;
    u.u_error = 0;
    swap_ioctl_test_reset();
    hk_reset_output();
}

static unsigned long
free_total(void)
{
    struct mapent *entry;
    unsigned long total = 0;

    for (entry = swapmap[0].m_map; entry->m_size != 0; entry++)
        total += entry->m_size;
    return total;
}

static int
call_ioctl(int unit, u_int command, void *value, int flag)
{
    return swcioctl(makedev(4, unit), command, (caddr_t)value, flag);
}

static void
positive_allocation(void)
{
    off_t blocks = 5;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(blocks == 5);
    HK_CHECK(swap_ioctl_test_start(0) == MAP_START);
    HK_CHECK(swap_ioctl_test_size(0) == 5);
    HK_CHECK(swap_ioctl_test_next(0) == 0);
    HK_CHECK(free_total() == MAP_BLOCKS - 8);
    HK_CHECK(swapnext == MAP_START + 8);
    HK_CHECK(cursor_publications == 1);
    HK_CHECK(published_cursor == swapnext);
}

static void
zero_releases_with_success(void)
{
    off_t blocks = 5;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    blocks = 0;
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(blocks == 0);
    HK_CHECK(swap_ioctl_test_start(0) == 0);
    HK_CHECK(swap_ioctl_test_size(0) == 0);
    HK_CHECK(swap_ioctl_test_next(0) == APPEND_DISABLED);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(cursor_publications == 1);
}

static void
media_size_returns_success(void)
{
    off_t blocks = 5;
    u_int media_size = 0xa5a5a5a5U;

    reset_fixture();
    HK_CHECK(call_ioctl(1, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(call_ioctl(1, DIOCGETMEDIASIZE, &media_size, FREAD) == 0);
    HK_CHECK(media_size == 5);
    HK_CHECK(swap_ioctl_test_size(1) == 5);
}

static void
negative_request_releases_then_rejects(void)
{
    off_t blocks = 5;

    reset_fixture();
    HK_CHECK(call_ioctl(2, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    blocks = -7;
    HK_CHECK(call_ioctl(2, TFALLOC, &blocks, FREAD | FWRITE) == EINVAL);
    HK_CHECK(blocks == -7);
    HK_CHECK(swap_ioctl_test_start(2) == 0);
    HK_CHECK(swap_ioctl_test_size(2) == 0);
    HK_CHECK(swap_ioctl_test_next(2) == APPEND_DISABLED);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(cursor_publications == 1);
}

static void
shortage_reports_zero_with_success(void)
{
    off_t blocks = MAP_BLOCKS + 1;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(blocks == 0);
    HK_CHECK(swap_ioctl_test_start(0) == 0);
    HK_CHECK(swap_ioctl_test_size(0) == 0);
    HK_CHECK(swap_ioctl_test_next(0) == APPEND_DISABLED);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(swapnext == MAP_START);
    HK_CHECK(cursor_publications == 0);
}

static void
replacement_shortage_leaves_device_empty(void)
{
    off_t blocks = 5;
    size_t cursor_after_allocation;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    cursor_after_allocation = swapnext;
    blocks = MAP_BLOCKS + 1;
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(blocks == 0);
    HK_CHECK(swap_ioctl_test_start(0) == 0);
    HK_CHECK(swap_ioctl_test_size(0) == 0);
    HK_CHECK(swap_ioctl_test_next(0) == APPEND_DISABLED);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(swapnext == cursor_after_allocation);
    HK_CHECK(cursor_publications == 1);
}

static void
invalid_requests_preserve_state(void)
{
    off_t blocks = 5;
    u_int payload = 0x5a5a5a5aU;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    HK_CHECK(call_ioctl(0, 0x12345678U, &payload, FREAD) == EINVAL);
    HK_CHECK(payload == 0x5a5a5a5aU);
    HK_CHECK(swap_ioctl_test_start(0) == MAP_START);
    HK_CHECK(swap_ioctl_test_size(0) == 5);
    HK_CHECK(free_total() == MAP_BLOCKS - 8);

    HK_CHECK(call_ioctl(3, DIOCGETMEDIASIZE, &payload, FREAD) == ENODEV);
    HK_CHECK(payload == 0x5a5a5a5aU);
    HK_CHECK(call_ioctl(64, DIOCGETMEDIASIZE, &payload, FREAD) == ENODEV);
    HK_CHECK(payload == 0x5a5a5a5aU);
}

/*
 * A read-only descriptor changes nothing, whoever holds it: the held extent,
 * the map, the cursor and the caller's request all stay as they were.
 */
static void
read_only_descriptor_is_refused(void)
{
    off_t blocks = 5;
    off_t request;

    reset_fixture();
    HK_CHECK(call_ioctl(0, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    request = 9;
    HK_CHECK(call_ioctl(0, TFALLOC, &request, FREAD) == EBADF);
    HK_CHECK(request == 9);
    request = 0;
    HK_CHECK(call_ioctl(0, TFALLOC, &request, FREAD) == EBADF);
    HK_CHECK(swap_ioctl_test_start(0) == MAP_START);
    HK_CHECK(swap_ioctl_test_size(0) == 5);
    HK_CHECK(free_total() == MAP_BLOCKS - 8);
    HK_CHECK(swapnext == MAP_START + 8);
    HK_CHECK(cursor_publications == 1);

    reset_fixture();
    request = 5;
    HK_CHECK(call_ioctl(1, TFALLOC, &request, FREAD) == EBADF);
    HK_CHECK(request == 5);
    HK_CHECK(swap_ioctl_test_start(1) == 0);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(cursor_publications == 0);
}

/*
 * An ordinary user holding a writable descriptor can neither allocate, nor
 * replace, nor release the extent root left on the unit.
 */
static void
non_superuser_is_refused(void)
{
    off_t blocks = 5;
    off_t request;

    reset_fixture();
    HK_CHECK(call_ioctl(2, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    allow_superuser = 0;
    request = 0;
    HK_CHECK(call_ioctl(2, TFALLOC, &request, FREAD | FWRITE) == EPERM);
    request = 9;
    HK_CHECK(call_ioctl(2, TFALLOC, &request, FREAD | FWRITE) == EPERM);
    HK_CHECK(request == 9);
    HK_CHECK(superuser_calls == 3);
    HK_CHECK(swap_ioctl_test_start(2) == MAP_START);
    HK_CHECK(swap_ioctl_test_size(2) == 5);
    HK_CHECK(free_total() == MAP_BLOCKS - 8);
    HK_CHECK(cursor_publications == 1);

    reset_fixture();
    allow_superuser = 0;
    request = 5;
    HK_CHECK(call_ioctl(0, TFALLOC, &request, FREAD | FWRITE) == EPERM);
    HK_CHECK(swap_ioctl_test_start(0) == 0);
    HK_CHECK(free_total() == MAP_BLOCKS);
    HK_CHECK(cursor_publications == 0);
}

/* The size query stays open to any reader. */
static void
media_size_needs_no_privilege(void)
{
    off_t blocks = 5;
    u_int media_size = 0xa5a5a5a5U;

    reset_fixture();
    HK_CHECK(call_ioctl(1, TFALLOC, &blocks, FREAD | FWRITE) == 0);
    allow_superuser = 0;
    superuser_calls = 0;
    HK_CHECK(call_ioctl(1, DIOCGETMEDIASIZE, &media_size, FREAD) == 0);
    HK_CHECK(media_size == 5);
    HK_CHECK(superuser_calls == 0);
}

/*
 * At securelevel 1 and above minor 64 takes no write open and no write
 * transfer, whenever its descriptor was opened; reads, level 0 and the
 * temporary units are unaffected.
 */
static void
live_swap_writes_follow_securelevel(void)
{
    dev_t live = makedev(4, 64);
    struct buf bp;

    for (int level = 0; level <= 2; level++) {
        securelevel = level;
        live_opens = live_transfers = completions = 0;
        HK_CHECK(swopen(live, FWRITE, 0) == (level ? EPERM : 0));
        HK_CHECK(swopen(live, FREAD, 0) == 0);
        HK_CHECK(live_opens == (level ? 1U : 2U));

        bzero(&bp, sizeof(bp));
        bp.b_dev = live;
        bp.b_flags = B_WRITE;
        swstrategy(&bp);
        HK_CHECK(live_transfers == (level ? 0U : 1U));
        HK_CHECK(level ? (bp.b_flags & B_ERROR) && bp.b_error == EPERM &&
            completions == 1 : (bp.b_flags & B_ERROR) == 0);
        HK_CHECK(bp.b_dev == live);

        bzero(&bp, sizeof(bp));
        bp.b_dev = live;
        bp.b_flags = B_READ;
        swstrategy(&bp);
        HK_CHECK(live_transfers == (level ? 1U : 2U));
        HK_CHECK((bp.b_flags & B_ERROR) == 0);

        HK_CHECK(swopen(makedev(4, 0), FWRITE, 0) == 0);
    }
    securelevel = 0;
}

int
main(void)
{
    positive_allocation();
    zero_releases_with_success();
    media_size_returns_success();
    negative_request_releases_then_rejects();
    shortage_reports_zero_with_success();
    replacement_shortage_leaves_device_empty();
    invalid_requests_preserve_state();
    read_only_descriptor_is_refused();
    non_superuser_is_refused();
    media_size_needs_no_privilege();
    live_swap_writes_follow_securelevel();
    return hk_verdict("swap ioctl result contract");
}
