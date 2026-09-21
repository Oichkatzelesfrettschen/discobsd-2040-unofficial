/*
 * Host gate for ufs_setattr() in sys/kern/ufs_fio.c. MNT_NOATIME governs
 * timestamps caused by reads; an explicit utimes(2) request remains an inode
 * update. The gate links the kernel source at the target's width and observes
 * the flags and times passed to iupdat().
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/user.h>

_Static_assert(sizeof(time_t) == 4, "the target uses four-byte timestamps");

struct user u;
struct mount mount[NMOUNT];
int securelevel;

static struct inode gate_inode;
static unsigned update_calls;
static time_t updated_atime;
static time_t updated_mtime;
static int updated_waitfor;

int
chown1(struct inode *ip, int uid, int gid)
{
	(void)ip;
	(void)uid;
	(void)gid;
	return 0;
}

int
chmod1(struct inode *ip, int mode)
{
	(void)ip;
	(void)mode;
	return 0;
}

void
itrunc(struct inode *ip, off_t length, int flags)
{
	(void)ip;
	(void)length;
	(void)flags;
}

int
iupdat(struct inode *ip, struct timeval *atime, struct timeval *mtime,
    int waitfor)
{
	HK_CHECK(ip == &gate_inode);
	update_calls++;
	updated_atime = atime->tv_sec;
	updated_mtime = mtime->tv_sec;
	updated_waitfor = waitfor;
	return 0;
}

static void
reset_fixture(int mount_flags)
{
	u = (struct user){0};
	mount[0] = (struct mount){0};
	gate_inode = (struct inode){0};
	update_calls = 0;
	updated_atime = 0;
	updated_mtime = 0;
	updated_waitfor = 0;

	u.u_uid = 23;
	gate_inode.i_uid = u.u_uid;
	gate_inode.i_mode = IFREG | 0600;
	mount[0].m_filsys.fs_flags = mount_flags;
	mount[0].m_filsys.fs_ronly = (mount_flags & MNT_RDONLY) != 0;
}

static struct vattr
explicit_times(time_t atime, time_t mtime)
{
	struct vattr attributes;

	VATTR_NULL(&attributes);
	attributes.va_atime = atime;
	attributes.va_mtime = mtime;
	return attributes;
}

static void
test_atime_only(void)
{
	struct vattr attributes;

	reset_fixture(MNT_NOATIME);
	attributes = explicit_times(1234, (time_t)VNOVAL);
	HK_CHECK(ufs_setattr(&gate_inode, &attributes) == 0);
	HK_CHECK(update_calls == 1);
	HK_CHECK((gate_inode.i_flag & IACC) != 0);
	HK_CHECK((gate_inode.i_flag & (IUPD | ICHG)) == 0);
	HK_CHECK(updated_atime == 1234);
	HK_CHECK(updated_mtime == (time_t)VNOVAL);
	HK_CHECK(updated_waitfor == 1);
}

static void
test_atime_and_mtime(void)
{
	struct vattr attributes;

	reset_fixture(MNT_NOATIME);
	attributes = explicit_times(2345, 3456);
	HK_CHECK(ufs_setattr(&gate_inode, &attributes) == 0);
	HK_CHECK(update_calls == 1);
	HK_CHECK((gate_inode.i_flag & IACC) != 0);
	HK_CHECK((gate_inode.i_flag & (IUPD | ICHG)) == (IUPD | ICHG));
	HK_CHECK(updated_atime == 2345);
	HK_CHECK(updated_mtime == 3456);
}

static void
test_no_timestamp_request(void)
{
	struct vattr attributes;

	reset_fixture(MNT_NOATIME);
	attributes = explicit_times((time_t)VNOVAL, (time_t)VNOVAL);
	HK_CHECK(ufs_setattr(&gate_inode, &attributes) == 0);
	HK_CHECK(update_calls == 0);
	HK_CHECK((gate_inode.i_flag & (IACC | IUPD | ICHG)) == 0);
}

static void
test_read_only_mount(void)
{
	struct vattr attributes;

	reset_fixture(MNT_RDONLY | MNT_NOATIME);
	attributes = explicit_times(4567, (time_t)VNOVAL);
	HK_CHECK(ufs_setattr(&gate_inode, &attributes) == EROFS);
	HK_CHECK(update_calls == 0);
	HK_CHECK((gate_inode.i_flag & IACC) == 0);
}

int
main(void)
{
	test_atime_only();
	test_atime_and_mtime();
	test_no_timestamp_request();
	test_read_only_mount();
	return hk_verdict("ufs explicit timestamp contract");
}
