/*
 * Host gate for sys/kern/kern_subr.c, the uio machinery every read and write
 * path moves its bytes through.
 *
 * The gate links the kernel source itself. What it pins is the direction of
 * each copy, the walk across an iovec list, the accounting the callers read
 * back (uio_resid, uio_offset, and each iovec's own base and length), the two
 * panics that mark a uio whose iovec count has run out, and the sign of the
 * byte uwritec() returns, which callers store into an int.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/uio.h>

#define SEGMENTS	4
#define SEGMENT_BYTES	8

static struct iovec vectors[SEGMENTS];
static struct uio transfer;
static char user_space[SEGMENTS][SEGMENT_BYTES];
static char kernel_space[SEGMENTS * SEGMENT_BYTES];

/*
 * Lay out a transfer over count segments of SEGMENT_BYTES each. User space
 * fills with a distinct byte per segment; kernel space fills with a counting
 * pattern, so a copy in either direction is visible at both ends.
 */
static void
reset(int count, enum uio_rw direction)
{
	int i, j;

	for (i = 0; i < SEGMENTS; i++) {
		for (j = 0; j < SEGMENT_BYTES; j++)
			user_space[i][j] = (char)('A' + i);
		vectors[i].iov_base = user_space[i];
		vectors[i].iov_len = SEGMENT_BYTES;
	}
	for (i = 0; i < SEGMENTS * SEGMENT_BYTES; i++)
		kernel_space[i] = (char)i;

	transfer.uio_iov = vectors;
	transfer.uio_iovcnt = count;
	transfer.uio_offset = 0;
	transfer.uio_resid = (u_int)(count * SEGMENT_BYTES);
	transfer.uio_rw = direction;
	hk_reset_output();
}

/* Every byte of user space across count segments, as one flat compare. */
static int
user_holds(int count, const char *expected)
{
	int i, j, k = 0;

	for (i = 0; i < count; i++)
		for (j = 0; j < SEGMENT_BYTES; j++)
			if (user_space[i][j] != expected[k++])
				return 0;
	return 1;
}

/*
 * UIO_READ moves kernel bytes out to the segments, in order, and charges each
 * segment's length and the transfer's residual and offset as it goes.
 */
static void
read_fills_every_segment(void)
{
	int i;

	reset(SEGMENTS, UIO_READ);
	HK_CHECK(uiomove(kernel_space, SEGMENTS * SEGMENT_BYTES,
	    &transfer) == 0);
	HK_CHECK(user_holds(SEGMENTS, kernel_space));
	HK_CHECK(transfer.uio_resid == 0);
	HK_CHECK(transfer.uio_offset == SEGMENTS * SEGMENT_BYTES);
	for (i = 0; i < SEGMENTS; i++) {
		HK_CHECK(vectors[i].iov_len == 0);
		HK_CHECK(vectors[i].iov_base ==
		    user_space[i] + SEGMENT_BYTES);
	}
}

/* UIO_WRITE is the same walk in the other direction. */
static void
write_drains_every_segment(void)
{
	int i, j;

	reset(SEGMENTS, UIO_WRITE);
	HK_CHECK(uiomove(kernel_space, SEGMENTS * SEGMENT_BYTES,
	    &transfer) == 0);
	for (i = 0; i < SEGMENTS; i++)
		for (j = 0; j < SEGMENT_BYTES; j++)
			HK_CHECK(kernel_space[i * SEGMENT_BYTES + j] ==
			    (char)('A' + i));
	HK_CHECK(transfer.uio_resid == 0);
	HK_CHECK(transfer.uio_offset == SEGMENTS * SEGMENT_BYTES);
}

/*
 * A count smaller than the residual stops mid-segment and leaves the rest of
 * that segment addressable, which is how a driver hands back a short read.
 */
static void
short_count_stops_mid_segment(void)
{
	reset(SEGMENTS, UIO_READ);
	HK_CHECK(uiomove(kernel_space, SEGMENT_BYTES + 3, &transfer) == 0);
	HK_CHECK(transfer.uio_resid ==
	    (u_int)(SEGMENTS * SEGMENT_BYTES - SEGMENT_BYTES - 3));
	HK_CHECK(transfer.uio_offset == SEGMENT_BYTES + 3);
	HK_CHECK(vectors[0].iov_len == 0);
	HK_CHECK(vectors[1].iov_len == (size_t)(SEGMENT_BYTES - 3));
	HK_CHECK(vectors[1].iov_base == user_space[1] + 3);
	/* The untouched tail of the second segment still holds its own byte. */
	HK_CHECK(user_space[1][3] == 'B');
	HK_CHECK(user_space[2][0] == 'C');
}

/*
 * A residual of zero ends the walk even when the caller offers more bytes,
 * and a count of zero moves nothing. Both are how a caller spells "no room"
 * and "nothing to do" without a separate return value.
 */
static void
exhausted_transfer_moves_nothing(void)
{
	reset(SEGMENTS, UIO_READ);
	transfer.uio_resid = 0;
	HK_CHECK(uiomove(kernel_space, SEGMENTS * SEGMENT_BYTES,
	    &transfer) == 0);
	HK_CHECK(transfer.uio_offset == 0);
	HK_CHECK(vectors[0].iov_len == SEGMENT_BYTES);
	HK_CHECK(user_space[0][0] == 'A');

	reset(SEGMENTS, UIO_READ);
	HK_CHECK(uiomove(kernel_space, 0, &transfer) == 0);
	HK_CHECK(transfer.uio_resid == (u_int)(SEGMENTS * SEGMENT_BYTES));
	HK_CHECK(user_space[0][0] == 'A');
}

/*
 * An empty segment costs one iovec and no bytes. A caller that builds a list
 * with holes in it still gets its data placed in the segments that have room.
 */
static void
empty_segments_are_skipped(void)
{
	reset(SEGMENTS, UIO_READ);
	vectors[0].iov_len = 0;
	vectors[2].iov_len = 0;
	transfer.uio_resid = (u_int)(2 * SEGMENT_BYTES);

	HK_CHECK(uiomove(kernel_space, 2 * SEGMENT_BYTES, &transfer) == 0);
	HK_CHECK(transfer.uio_resid == 0);
	HK_CHECK(user_space[0][0] == 'A');
	HK_CHECK(user_space[2][0] == 'C');
	HK_CHECK(user_space[1][0] == kernel_space[0]);
	HK_CHECK(user_space[3][0] == kernel_space[SEGMENT_BYTES]);
}

/*
 * ureadc() places one byte and steps over any segment with no room. The
 * segment list it walks is the caller's, so the accounting has to match what
 * a following uiomove() would see.
 */
static void
single_byte_read(void)
{
	reset(SEGMENTS, UIO_READ);
	HK_CHECK(ureadc('z', &transfer) == 0);
	HK_CHECK(user_space[0][0] == 'z');
	HK_CHECK(vectors[0].iov_len == (size_t)(SEGMENT_BYTES - 1));
	HK_CHECK(vectors[0].iov_base == user_space[0] + 1);
	HK_CHECK(transfer.uio_resid == (u_int)(SEGMENTS * SEGMENT_BYTES - 1));
	HK_CHECK(transfer.uio_offset == 1);

	/* An empty leading segment is stepped over, spending one iovec. */
	reset(SEGMENTS, UIO_READ);
	vectors[0].iov_len = 0;
	HK_CHECK(ureadc('q', &transfer) == 0);
	HK_CHECK(user_space[1][0] == 'q');
	HK_CHECK(transfer.uio_iovcnt == SEGMENTS - 1);
	HK_CHECK(transfer.uio_iov == &vectors[1]);
}

/*
 * uwritec() returns the byte widened without sign, so a caller storing it in
 * an int distinguishes 0xff from the -1 that means the transfer is spent.
 */
static void
single_byte_write(void)
{
	reset(SEGMENTS, UIO_WRITE);
	user_space[0][0] = (char)0xff;
	HK_CHECK(uwritec(&transfer) == 0xff);
	HK_CHECK(transfer.uio_resid == (u_int)(SEGMENTS * SEGMENT_BYTES - 1));
	HK_CHECK(transfer.uio_offset == 1);
	HK_CHECK(vectors[0].iov_len == (size_t)(SEGMENT_BYTES - 1));

	/* A spent transfer answers -1 rather than a byte. */
	reset(SEGMENTS, UIO_WRITE);
	transfer.uio_resid = 0;
	HK_CHECK(uwritec(&transfer) == -1);

	/* So does a list whose last segment is empty. */
	reset(1, UIO_WRITE);
	vectors[0].iov_len = 0;
	HK_CHECK(uwritec(&transfer) == -1);

	/* An empty segment before a full one is stepped over. */
	reset(SEGMENTS, UIO_WRITE);
	vectors[0].iov_len = 0;
	user_space[1][0] = 'k';
	HK_CHECK(uwritec(&transfer) == 'k');
	HK_CHECK(transfer.uio_iovcnt == SEGMENTS - 1);
}

static void
call_ureadc_without_segments(void)
{
	(void)ureadc('x', &transfer);
}

static void
call_uwritec_without_segments(void)
{
	(void)uwritec(&transfer);
}

/*
 * A uio whose residual outlives its segment list is a caller error, and both
 * single-byte routines say so rather than walking past the array. This is the
 * condition uiomove() does not check, which is why a caller must keep the
 * residual equal to the sum of the segment lengths.
 */
static void
spent_segment_list_panics(void)
{
	reset(SEGMENTS, UIO_READ);
	transfer.uio_iovcnt = 0;
	HK_EXPECT_PANIC("ureadc", call_ureadc_without_segments);

	reset(SEGMENTS, UIO_WRITE);
	transfer.uio_iovcnt = 0;
	HK_EXPECT_PANIC("uwritec", call_uwritec_without_segments);
}

/*
 * uiofmove() copies between one segment and the kernel without touching the
 * transfer's accounting, which is what its callers do their own bookkeeping
 * around.
 */
static void
direct_segment_move(void)
{
	reset(SEGMENTS, UIO_READ);
	HK_CHECK(uiofmove(kernel_space, 4, &transfer, &vectors[0]) == 0);
	HK_CHECK(user_space[0][0] == kernel_space[0]);
	HK_CHECK(user_space[0][3] == kernel_space[3]);
	HK_CHECK(user_space[0][4] == 'A');
	HK_CHECK(transfer.uio_resid == (u_int)(SEGMENTS * SEGMENT_BYTES));
	HK_CHECK(transfer.uio_offset == 0);
	HK_CHECK(vectors[0].iov_len == SEGMENT_BYTES);

	reset(SEGMENTS, UIO_WRITE);
	HK_CHECK(uiofmove(kernel_space, 4, &transfer, &vectors[2]) == 0);
	HK_CHECK(kernel_space[0] == 'C');
	HK_CHECK(kernel_space[3] == 'C');
	HK_CHECK(kernel_space[4] == 4);
}

int
main(void)
{
	read_fills_every_segment();
	write_drains_every_segment();
	short_count_stops_mid_segment();
	exhausted_transfer_moves_nothing();
	empty_segments_are_skipped();
	single_byte_read();
	single_byte_write();
	spent_segment_list_panics();
	direct_segment_move();
	return hk_verdict("kern_subr");
}
