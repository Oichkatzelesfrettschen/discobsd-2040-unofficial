/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#define SHUTDOWN_DRAIN_PASSES 20

extern int waittime;

static int
shutdown_busy_buffers(void)
{
	int buffer_index, busy_count;

	busy_count = 0;
	for (buffer_index = 0; buffer_index < NBUF; buffer_index++)
		if (buf[buffer_index].b_flags & B_BUSY)
			busy_count++;
	return busy_count;
}

static void
shutdown_report_write_errors(void)
{
	struct mount *mount_entry;

	for (mount_entry = mount; mount_entry < &mount[NMOUNT]; mount_entry++) {
		if (mount_entry->m_inodp == NULL ||
		    mount_entry->m_write_error == 0)
			continue;
		printf("recorded write error on dev %o: error %d\n",
		    mount_entry->m_dev, mount_entry->m_write_error);
	}
}

void
rp2040_shutdown_sync(int howto)
{
	struct fs *root_filesystem;
	int busy_count, pass;

	if ((howto & RB_NOSYNC) != 0 || waittime >= 0 ||
	    bfreelist[0].av_forw == NULL)
		return;

	root_filesystem = getfs(rootdev);
	if (root_filesystem != NULL)
		root_filesystem->fs_fmod = 1;
	waittime = 0;
	printf("syncing disks... ");
	(void)splnet();
	sync();

	busy_count = shutdown_busy_buffers();
	for (pass = 0; busy_count != 0 && pass < SHUTDOWN_DRAIN_PASSES;
	    pass++) {
		printf("%d ", busy_count);
		mdelay(40U * (unsigned)pass);
		busy_count = shutdown_busy_buffers();
	}
	if (busy_count == 0)
		printf("buffers idle\n");
	else
		printf("timeout: %d buffers busy\n", busy_count);

	shutdown_report_write_errors();
}
