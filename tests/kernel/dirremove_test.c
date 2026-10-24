/*
 * ILP32 contract for publishing a directory-entry removal only after the
 * backing write succeeds. The fixture links the production dirremove()
 * provider and controls the two storage paths below that provider.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

struct user u;
struct mount mount[NMOUNT];

static struct inode parent_inode;
static struct nameidata name;
static union {
    struct direct alignment;
    char bytes[DEV_BSIZE];
} directory_storage;
static struct direct *previous_entry;
static struct buf directory_buffer;
static int rdwri_error;
static int bwrite_error;
static unsigned rdwri_calls;
static unsigned bmap_calls;
static unsigned bread_calls;
static unsigned brelse_calls;
static unsigned bwrite_calls;

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t bytes, int length,
    off_t offset, int flags, int *residual)
{
    struct direct *entry = (struct direct *)bytes;

    HK_CHECK(operation == UIO_WRITE);
    HK_CHECK(inode == &parent_inode);
    HK_CHECK((unsigned)length == DIRSIZ(&name.ni_dent));
    HK_CHECK(offset == name.ni_offset);
    HK_CHECK(flags == (IO_UNIT | IO_SYNC));
    HK_CHECK(residual == NULL);
    HK_CHECK(entry->d_ino == 0);
    rdwri_calls++;
    return rdwri_error;
}

daddr_t
bmap(struct inode *inode, daddr_t block, int operation, int size)
{
    HK_CHECK(inode == &parent_inode);
    HK_CHECK(block == 0);
    HK_CHECK(operation == B_READ);
    HK_CHECK(size == 0);
    bmap_calls++;
    return 7;
}

struct buf *
bread(dev_t device, daddr_t block)
{
    HK_CHECK(device == mount[0].m_dev);
    HK_CHECK(block == 7);
    bread_calls++;
    return &directory_buffer;
}

void
brelse(struct buf *buffer)
{
    HK_CHECK(buffer == &directory_buffer);
    brelse_calls++;
}

int
bwrite(struct buf *buffer)
{
    HK_CHECK(buffer == &directory_buffer);
    bwrite_calls++;
    return bwrite_error;
}

static void
reset_fixture(u_short distance_from_previous)
{
    u = (struct user){0};
    parent_inode = (struct inode){ .i_count = 1, .i_mode = IFDIR };
    name = (struct nameidata){
        .ni_pdir = &parent_inode,
        .ni_offset = 64,
        .ni_count = distance_from_previous,
        .ni_dent = {
            .d_ino = 42,
            .d_reclen = 24,
            .d_namlen = 4,
            .d_name = "leaf",
        },
    };
    mount[0] = (struct mount){ .m_dev = 3 };
    bzero(&directory_storage, sizeof(directory_storage));
    previous_entry = (struct direct *)(directory_storage.bytes + 48);
    *previous_entry = (struct direct){
        .d_ino = 11,
        .d_reclen = 16,
        .d_namlen = 3,
        .d_name = "old",
    };
    directory_buffer = (struct buf){ .b_addr = directory_storage.bytes };
    rdwri_error = 0;
    bwrite_error = 0;
    rdwri_calls = 0;
    bmap_calls = 0;
    bread_calls = 0;
    brelse_calls = 0;
    bwrite_calls = 0;
}

static void
first_entry_requires_successful_write(void)
{
    reset_fixture(0);
    HK_CHECK(dirremove(&name) == 1);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(rdwri_calls == 1);
    HK_CHECK(bmap_calls == 0 && bread_calls == 0 && bwrite_calls == 0);

    reset_fixture(0);
    rdwri_error = EIO;
    HK_CHECK(dirremove(&name) == 0);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(rdwri_calls == 1);
    HK_CHECK((parent_inode.i_flag & (IUPD | ICHG)) == 0);
}

static void
coalesced_entry_requires_successful_write(void)
{
    reset_fixture(16);
    HK_CHECK(dirremove(&name) == 1);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(bmap_calls == 1 && bread_calls == 1 && bwrite_calls == 1);
    HK_CHECK(previous_entry->d_reclen == 40);
    HK_CHECK((parent_inode.i_flag & (IUPD | ICHG)) == (IUPD | ICHG));

    reset_fixture(16);
    bwrite_error = EIO;
    HK_CHECK(dirremove(&name) == 0);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(bmap_calls == 1 && bread_calls == 1 && bwrite_calls == 1);
    HK_CHECK((parent_inode.i_flag & (IUPD | ICHG)) == 0);
}

int
main(void)
{
    first_entry_requires_successful_write();
    coalesced_entry_requires_successful_write();
    return hk_verdict("directory removal write publication");
}
