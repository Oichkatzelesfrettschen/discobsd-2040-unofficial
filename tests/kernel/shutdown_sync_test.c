/*
 * Exercise the RP2040 shutdown synchronizer from its production source.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#include <machine/machparam.h>

#define MAX_DELAYS 20

struct buf buf[NBUF];
struct buf bfreelist[BQUEUES];
struct mount mount[NMOUNT];
dev_t rootdev;
int waittime;

static struct fs root_filesystem;
static struct inode mounted_inodes[NMOUNT];
static unsigned delay_calls;
static unsigned delay_values[MAX_DELAYS];
static unsigned complete_after_delay;
static unsigned sync_calls;
static int sync_interrupt_level;
static int sync_write_error;
static int sync_reenters;
static int nested_reentry_returned;
static int root_available;

static void
clear_busy_buffers(void)
{
	int buffer_index;

	for (buffer_index = 0; buffer_index < NBUF; buffer_index++)
		buf[buffer_index].b_flags &= ~B_BUSY;
}

static void
set_busy_buffers(int count)
{
	int buffer_index;

	clear_busy_buffers();
	for (buffer_index = 0; buffer_index < count; buffer_index++)
		buf[buffer_index].b_flags |= B_BUSY;
}

struct fs *
getfs(dev_t device)
{
	HK_CHECK(device == rootdev);
	return root_available ? &root_filesystem : NULL;
}

void
sync(void)
{
	sync_calls++;
	sync_interrupt_level = hk_ipl;
	if (sync_reenters) {
		sync_reenters = 0;
		rp2040_shutdown_sync(0);
		nested_reentry_returned = 1;
	}
	if (sync_write_error != 0)
		mount[0].m_write_error = sync_write_error;
}

void
mdelay(unsigned milliseconds)
{
	HK_CHECK(delay_calls < MAX_DELAYS);
	delay_values[delay_calls++] = milliseconds;
	if (complete_after_delay != 0 &&
	    delay_calls == complete_after_delay)
		clear_busy_buffers();
}

static void
reset_case(void)
{
	int buffer_index, mount_index;

	for (buffer_index = 0; buffer_index < NBUF; buffer_index++)
		buf[buffer_index].b_flags = 0;
	for (mount_index = 0; mount_index < NMOUNT; mount_index++) {
		mount[mount_index].m_dev = 0;
		mount[mount_index].m_inodp = NULL;
		mount[mount_index].m_write_error = 0;
	}
	for (buffer_index = 0; buffer_index < MAX_DELAYS; buffer_index++)
		delay_values[buffer_index] = 0;
	bfreelist[0].av_forw = &bfreelist[0];
	rootdev = 0301;
	waittime = -1;
	root_filesystem.fs_fmod = 0;
	delay_calls = 0;
	complete_after_delay = 0;
	sync_calls = 0;
	sync_interrupt_level = 0;
	sync_write_error = 0;
	sync_reenters = 0;
	nested_reentry_returned = 0;
	root_available = 1;
	hk_ipl = 0;
	hk_ipl_raises = 0;
	hk_reset_output();
}

static void
check_immediate_idle(void)
{
	reset_case();
	rp2040_shutdown_sync(0);
	HK_CHECK(waittime == 0);
	HK_CHECK(root_filesystem.fs_fmod == 1);
	HK_CHECK(sync_calls == 1);
	HK_CHECK(sync_interrupt_level == 1);
	HK_CHECK(hk_ipl == 1);
	HK_CHECK(delay_calls == 0);
	HK_CHECK(hk_contains(hk_printf_text, "syncing disks... buffers idle\n"));
	HK_CHECK(!hk_contains(hk_printf_text, "timeout"));
}

static void
check_delayed_completion(void)
{
	reset_case();
	set_busy_buffers(2);
	complete_after_delay = 3;
	rp2040_shutdown_sync(0);
	HK_CHECK(delay_calls == 3);
	HK_CHECK(delay_values[0] == 0);
	HK_CHECK(delay_values[1] == 40);
	HK_CHECK(delay_values[2] == 80);
	HK_CHECK(hk_contains(hk_printf_text, "buffers idle\n"));
}

static void
check_final_delay_completion(void)
{
	reset_case();
	set_busy_buffers(1);
	complete_after_delay = MAX_DELAYS;
	rp2040_shutdown_sync(0);
	HK_CHECK(delay_calls == MAX_DELAYS);
	HK_CHECK(delay_values[MAX_DELAYS - 1] == 760);
	HK_CHECK(hk_contains(hk_printf_text, "buffers idle\n"));
	HK_CHECK(!hk_contains(hk_printf_text, "timeout"));
}

static void
check_timeout(void)
{
	reset_case();
	set_busy_buffers(2);
	rp2040_shutdown_sync(0);
	HK_CHECK(delay_calls == MAX_DELAYS);
	HK_CHECK(delay_values[MAX_DELAYS - 1] == 760);
	HK_CHECK(hk_contains(hk_printf_text,
	    "timeout: 2 buffers busy\n"));
	HK_CHECK(!hk_contains(hk_printf_text, "buffers idle"));
}

static void
check_write_errors(void)
{
	reset_case();
	mount[0].m_dev = 0301;
	mount[0].m_inodp = &mounted_inodes[0];
	mount[0].m_write_error = EIO;
#if NMOUNT > 1
	mount[1].m_dev = 0402;
	mount[1].m_inodp = &mounted_inodes[1];
	mount[1].m_write_error = ENOSPC;
#endif
	rp2040_shutdown_sync(0);
	HK_CHECK(hk_contains(hk_printf_text,
	    "recorded write error on dev 301: error 5\n"));
#if NMOUNT > 1
		HK_CHECK(hk_contains(hk_printf_text,
		    "recorded write error on dev 402: error 28\n"));
#endif
	HK_CHECK(mount[0].m_write_error == EIO);
#if NMOUNT > 1
		HK_CHECK(mount[1].m_write_error == ENOSPC);
#endif
}

static void
check_inactive_mount(void)
{
	reset_case();
	mount[0].m_dev = 0301;
	mount[0].m_write_error = EIO;
	rp2040_shutdown_sync(0);
	HK_CHECK(!hk_contains(hk_printf_text, "write error"));
	HK_CHECK(mount[0].m_write_error == EIO);
}

static void
check_error_recorded_by_sync(void)
{
	reset_case();
	mount[0].m_dev = 0301;
	mount[0].m_inodp = &mounted_inodes[0];
	sync_write_error = EIO;
	rp2040_shutdown_sync(0);
	HK_CHECK(hk_contains(hk_printf_text,
	    "recorded write error on dev 301: error 5\n"));
	HK_CHECK(mount[0].m_write_error == EIO);
}

static void
check_timeout_with_error(void)
{
	reset_case();
	set_busy_buffers(1);
	mount[0].m_dev = 0301;
	mount[0].m_inodp = &mounted_inodes[0];
	mount[0].m_write_error = EIO;
	rp2040_shutdown_sync(0);
	HK_CHECK(hk_contains(hk_printf_text,
	    "timeout: 1 buffers busy\n"));
	HK_CHECK(hk_contains(hk_printf_text,
	    "recorded write error on dev 301: error 5\n"));
	HK_CHECK(mount[0].m_write_error == EIO);
}

static void
check_nested_reentry(void)
{
	reset_case();
	sync_reenters = 1;
	rp2040_shutdown_sync(0);
	HK_CHECK(sync_calls == 1);
	HK_CHECK(nested_reentry_returned == 1);
	HK_CHECK(hk_printf_calls == 2);
	HK_CHECK(hk_contains(hk_printf_text,
	    "syncing disks... buffers idle\n"));
}

static void
check_reentry(void)
{
	reset_case();
	rp2040_shutdown_sync(0);
	hk_reset_output();
	sync_calls = 0;
	delay_calls = 0;
	rp2040_shutdown_sync(0);
	HK_CHECK(sync_calls == 0);
	HK_CHECK(delay_calls == 0);
	HK_CHECK(hk_printf_calls == 0);
}

static void
check_uninitialized_buffers(void)
{
	reset_case();
	bfreelist[0].av_forw = NULL;
	rp2040_shutdown_sync(0);
	HK_CHECK(waittime == -1);
	HK_CHECK(sync_calls == 0);
	HK_CHECK(hk_printf_calls == 0);
}

static void
check_no_sync(void)
{
	reset_case();
	rp2040_shutdown_sync(RB_NOSYNC);
	HK_CHECK(waittime == -1);
	HK_CHECK(sync_calls == 0);
	HK_CHECK(hk_printf_calls == 0);
	HK_CHECK(hk_ipl == 0);
}

static void
check_missing_root(void)
{
	reset_case();
	root_available = 0;
	rp2040_shutdown_sync(0);
	HK_CHECK(waittime == 0);
	HK_CHECK(sync_calls == 1);
	HK_CHECK(root_filesystem.fs_fmod == 0);
	HK_CHECK(hk_contains(hk_printf_text, "buffers idle\n"));
}

int
main(void)
{
	check_immediate_idle();
	check_delayed_completion();
	check_final_delay_completion();
	check_timeout();
	check_write_errors();
	check_inactive_mount();
	check_error_recorded_by_sync();
	check_timeout_with_error();
	check_nested_reentry();
	check_reentry();
	check_uninitialized_buffers();
	check_no_sync();
	check_missing_root();
	return hk_verdict("rp2040 shutdown synchronization");
}
