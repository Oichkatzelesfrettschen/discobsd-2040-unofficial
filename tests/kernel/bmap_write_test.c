/* Production bmap: publish only initialized allocations, even on async roots. */
#include "hostkern.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/buf.h>
#include <sys/mount.h>

struct user u;
daddr_t rablock;
static struct inode node;
static struct fs fs;
static struct buf buffers[5];
static daddr_t contents[5][NINDIR];
static unsigned allocated, writes, delayed, released, freed, fail_write;
static daddr_t reclaimed;

struct buf *balloc(struct inode *ip, int flags)
{
    struct buf *bp;
    (void)ip;
    HK_CHECK(allocated < 4);
    bp = &buffers[++allocated];
    bp->b_blkno = 100 + allocated;
    bp->b_flags = B_BUSY;
    bp->b_addr = (char *)contents[allocated];
    if (flags & B_CLRBUF)
        bzero(bp->b_addr, DEV_BSIZE);
    return bp;
}

struct buf *bread(dev_t dev, daddr_t block)
{
    unsigned i;
    (void)dev;
    for (i = 0; i <= allocated; i++) {
        if (buffers[i].b_blkno == block) {
            buffers[i].b_flags |= B_BUSY;
            return &buffers[i];
        }
    }
    HK_CHECK(0);
    return &buffers[0];
}

void brelse(struct buf *bp)
{
    released++;
    bp->b_flags &= ~B_BUSY;
}

int bwrite(struct buf *bp)
{
    writes++;
    brelse(bp);
    /* Return-only failure: bmap must propagate the error itself. */
    return writes == fail_write ? EIO : 0;
}

void bdwrite(struct buf *bp)
{
    delayed++;
    bp->b_flags |= B_DELWRI;
    brelse(bp);
}

void free(struct inode *ip, daddr_t block)
{
    unsigned i, j;
    HK_CHECK(ip == &node);
    for (i = 0; i < NADDR; i++)
        HK_CHECK(ip->i_addr[i] != block);
    for (i = 0; i <= allocated; i++)
        for (j = 0; j < NINDIR; j++)
            HK_CHECK(contents[i][j] != block);
    freed++;
    reclaimed = block;
    /* Cleanup may fail too; retain the original initialization error. */
    u.u_error = ENOSPC;
}

static void reset(int mount_flags, unsigned failure)
{
    u = (struct user){0}; node = (struct inode){0}; fs = (struct fs){0};
    bzero(buffers, sizeof(buffers)); bzero(contents, sizeof(contents));
    allocated = writes = delayed = released = freed = 0;
    reclaimed = 0; fail_write = failure;
    node.i_fs = &fs; node.i_dev = 1; fs.fs_flags = mount_flags;
    buffers[0].b_blkno = 50; buffers[0].b_addr = (char *)contents[0];
}

static void failures(void)
{
    int modes[] = {0, MNT_ASYNC};
    unsigned m;
    for (m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        reset(modes[m], 1);
        HK_CHECK(bmap(&node, 0, B_WRITE, B_SYNC | B_CLRBUF) == -1);
        HK_CHECK(u.u_error == EIO && node.i_addr[0] == 0);
        HK_CHECK(freed == 1 && reclaimed == 101 && writes == 1 && delayed == 0);
        HK_CHECK(node.i_flag == 0 && !(buffers[1].b_flags & B_BUSY));

        reset(modes[m], 1);
        HK_CHECK(bmap(&node, NADDR-3, B_WRITE, B_SYNC) == -1);
        HK_CHECK(u.u_error == EIO && node.i_addr[NADDR-3] == 0);
        HK_CHECK(freed == 1 && reclaimed == 101 && writes == 1 && delayed == 0);
        HK_CHECK(node.i_flag == 0);

        /* An existing double-indirect root must not publish a failed child. */
        reset(modes[m], 1);
        node.i_addr[NADDR-2] = 50;
        HK_CHECK(bmap(&node, NADDR-3 + (1L << NSHIFT), B_WRITE, B_SYNC) == -1);
        HK_CHECK(u.u_error == EIO && node.i_addr[NADDR-2] == 50);
        HK_CHECK(contents[0][0] == 0 && freed == 1 && reclaimed == 101);
        HK_CHECK(writes == 1 && delayed == 0 && released == 2);
        HK_CHECK(!(buffers[0].b_flags & B_BUSY));

        /* Keep earlier initialized ancestors, but reclaim the unpublished child. */
        reset(modes[m], 2);
        HK_CHECK(bmap(&node, NADDR-3 + (1L << NSHIFT), B_WRITE, B_SYNC) == -1);
        HK_CHECK(u.u_error == EIO && node.i_addr[NADDR-2] == 101);
        HK_CHECK(contents[1][0] == 0 && freed == 1 && reclaimed == 102);
        HK_CHECK(writes == 2 && delayed == 0);
        HK_CHECK(!(buffers[1].b_flags & B_BUSY));

        /* Triple-indirect failure preserves both initialized ancestors. */
        reset(modes[m], 3);
        HK_CHECK(bmap(&node, NADDR-3 + (1L << NSHIFT) + (1L << (2 * NSHIFT)),
            B_WRITE, B_SYNC) == -1);
        HK_CHECK(u.u_error == EIO && node.i_addr[NADDR-1] == 101);
        HK_CHECK(contents[1][0] == 102 && contents[2][0] == 0);
        HK_CHECK(freed == 1 && reclaimed == 103 && writes == 3 && delayed == 1);
        HK_CHECK(!(buffers[2].b_flags & B_BUSY));

        /* The final data-block initialization is subject to the same rule. */
        reset(modes[m], 1);
        node.i_addr[NADDR-3] = 50;
        HK_CHECK(bmap(&node, NADDR-3, B_WRITE, B_SYNC) == -1);
        HK_CHECK(u.u_error == EIO && contents[0][0] == 0 && freed == 1);
        HK_CHECK(writes == 1 && delayed == 0 && !(buffers[0].b_flags & B_BUSY));
    }
}

static void successes(void)
{
    reset(MNT_ASYNC, 0);
    HK_CHECK(bmap(&node, NADDR-3 + (1L << NSHIFT), B_WRITE, B_SYNC) == 103);
    HK_CHECK(node.i_addr[NADDR-2] == 101 && contents[1][0] == 102);
    HK_CHECK(contents[2][0] == 103 && writes == 3 && freed == 0);
    HK_CHECK(delayed == 2 && u.u_error == 0);

    reset(MNT_ASYNC, 0);
    HK_CHECK(bmap(&node, NADDR-3, B_WRITE, 0) == 102);
    HK_CHECK(node.i_addr[NADDR-3] == 101 && contents[1][0] == 102);
    HK_CHECK(writes == 0 && delayed == 3 && freed == 0 && u.u_error == 0);
}

int main(void)
{
    failures(); successes();
    return hk_verdict("bmap publication and reclamation");
}
