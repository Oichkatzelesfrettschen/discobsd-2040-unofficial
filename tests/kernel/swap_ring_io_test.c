/*
 * Host gate for the block fold in swap_with_buf_result() (sys/kern/vm_swp.c).
 *
 * On a ring swap map an image extent may pass the last block and continue at
 * the first one (struct map, sys/map.h). The image code keeps one base and
 * adds offsets to it, so the transfer function is where a block at or above
 * m_ring_end becomes a device block and where a transfer that reaches the
 * last block ends and resumes.
 *
 * The gate includes the kernel source and supplies the swap device: a
 * strategy routine that records each request and completes it at once. The
 * linker drops the rest of vm_swp.c, which the gate does not call.
 */
#include "hostkern.h"

#include "../../sys/kern/vm_swp.c"

#define RING_FIRST	4
#define RING_END	384

struct request {
	size_t block;
	size_t address;
	int count;
};

static struct request requests[8];
static unsigned request_count;
static struct mapent entries[8];
struct map swapmap[1] = { { entries, &entries[6], "swapmap", 0, 0, 0 } };
dev_t swapdev;
struct vmrate cnt;

#ifdef SDSWAP_ENABLED
/* The device fails every request from this one on; -1 for a sound device. */
static int fail_from = -1;
static unsigned wakes;
char runin, runout;

void
wakeup(caddr_t channel)
{
	HK_CHECK(channel == (caddr_t)&runin || channel == (caddr_t)&runout);
	wakes++;
}
#endif

static void
record_strategy(struct buf *bp)
{
	/* A transfer that never ends would hang the gate; it fails instead. */
	if (request_count == 8)
		panic("swap ring io: transfer does not end");
#ifdef SDSWAP_ENABLED
	if (fail_from >= 0 && request_count >= (unsigned)fail_from) {
		request_count++;
		bp->b_flags |= B_ERROR | B_DONE;
		return;
	}
#endif
	requests[request_count].block = bp->b_blkno;
	requests[request_count].address = (size_t)bp->b_addr;
	requests[request_count].count = bp->b_bcount;
	request_count++;
	bp->b_resid = 0;
	bp->b_flags |= B_DONE;
}

const struct bdevsw bdevsw[1] = { { .d_strategy = record_strategy } };

void
sleep(caddr_t channel, int priority)
{
	(void)channel;
	(void)priority;
	panic("swap ring io: transfer slept");
}

static int
transfer(size_t block, size_t address, int count)
{
	static struct buf header;

	request_count = 0;
	return swap_with_buf_result(&header, block, address, count, B_READ);
}

static void
interval_map_is_one_transfer(void)
{
	swapmap->m_ring_first = 0;
	swapmap->m_ring_end = 0;
	HK_CHECK(transfer(300, 0x1000, 100 * DEV_BSIZE) == 0);
	HK_CHECK(request_count == 1);
	HK_CHECK(requests[0].block == 300);
	HK_CHECK(requests[0].count == 100 * DEV_BSIZE);
}

static void
ring_transfer_ends_at_the_last_block(void)
{
	swapmap->m_ring_first = RING_FIRST;
	swapmap->m_ring_end = RING_END;

	/* Blocks 300 to 399 are 300 to 383 and then 4 to 19. */
	HK_CHECK(transfer(300, 0x1000, 100 * DEV_BSIZE) == 0);
	HK_CHECK(request_count == 2);
	HK_CHECK(requests[0].block == 300);
	HK_CHECK(requests[0].address == 0x1000);
	HK_CHECK(requests[0].count == 84 * DEV_BSIZE);
	HK_CHECK(requests[1].block == RING_FIRST);
	HK_CHECK(requests[1].address == 0x1000 + 84 * DEV_BSIZE);
	HK_CHECK(requests[1].count == 16 * DEV_BSIZE);

	/* A transfer that fits below the end is not divided. */
	HK_CHECK(transfer(300, 0x1000, 84 * DEV_BSIZE) == 0);
	HK_CHECK(request_count == 1);
	HK_CHECK(requests[0].count == 84 * DEV_BSIZE);

	/* The tail keeps a byte count that is not a whole block. */
	HK_CHECK(transfer(380, 0x2000, 4 * DEV_BSIZE + 512) == 0);
	HK_CHECK(request_count == 2);
	HK_CHECK(requests[0].block == 380);
	HK_CHECK(requests[0].count == 4 * DEV_BSIZE);
	HK_CHECK(requests[1].block == RING_FIRST);
	HK_CHECK(requests[1].count == 512);
}

static void
ring_block_above_the_end_is_folded(void)
{
	swapmap->m_ring_first = RING_FIRST;
	swapmap->m_ring_end = RING_END;

	/* Block 400 names block 20; the stack and u segments start so. */
	HK_CHECK(transfer(400, 0x3000, 8 * DEV_BSIZE) == 0);
	HK_CHECK(request_count == 1);
	HK_CHECK(requests[0].block == 20);
	HK_CHECK(requests[0].count == 8 * DEV_BSIZE);

	HK_CHECK(transfer(RING_END, 0x3000, DEV_BSIZE) == 0);
	HK_CHECK(request_count == 1);
	HK_CHECK(requests[0].block == RING_FIRST);
}

#ifdef SDSWAP_ENABLED
static void
void_transfer(void)
{
	static struct buf header;

	request_count = 0;
	swap_with_buf(&header, 300, 0x1000, DEV_BSIZE, B_READ);
}

/*
 * A failed transfer (swap_lost, sys/systm.h). A fixed device keeps the
 * panic. A removable one sets swap_lost once, wakes the swapper on both of
 * its channels, lets swap_with_buf() return, and is asked for nothing again
 * in either lost state.
 */
static void
failed_transfer_on_a_removable_device(void)
{
	swapmap->m_ring_first = 0;
	swapmap->m_ring_end = 0;

	fail_from = 0;
	HK_CHECK(transfer(300, 0x1000, DEV_BSIZE) == -1);
	HK_CHECK(swap_lost == 0 && wakes == 0);
	HK_EXPECT_PANIC("hard err: swap", void_transfer);

	swap_removable = 1;
	HK_CHECK(transfer(300, 0x1000, DEV_BSIZE) == -1);
	HK_CHECK(request_count == 1);
	HK_CHECK(swap_lost == SWAP_LOST_NEW && wakes == 2);

	fail_from = -1;
	void_transfer();
	HK_CHECK(request_count == 0 && wakes == 2);
	HK_CHECK(transfer(300, 0x1000, DEV_BSIZE) == -1);
	HK_CHECK(request_count == 0 && swap_lost == SWAP_LOST_NEW);

	swap_lost = SWAP_LOST_SWEPT;
	void_transfer();
	HK_CHECK(transfer(300, 0x1000, DEV_BSIZE) == -1);
	HK_CHECK(request_count == 0 && wakes == 2);
	HK_CHECK(swap_lost == SWAP_LOST_SWEPT);
}
#endif

int
main(void)
{
	interval_map_is_one_transfer();
	ring_transfer_ends_at_the_last_block();
	ring_block_above_the_end_is_folded();
#ifdef SDSWAP_ENABLED
	failed_transfer_on_a_removable_device();
#endif
	return hk_verdict("swap ring io");
}
