/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

/*
 * sync() returns at once while another sync holds updlock and passes over a
 * mount whose superblock lists are locked, so the root device's checkpoint
 * (bdev_checkpoint(), DIOCCHECKPOINT) is forced here whatever sync() did:
 * the flash root keeps completed writes only up to its last checkpoint. A
 * failure latches in the root mount's m_write_error, which
 * shutdown_report_write_errors() prints, or is printed here when no mount
 * holds the root.
 */
static void
shutdown_checkpoint_root(void)
{
	struct mount *mount_entry;
	int error;

	error = bdev_checkpoint(rootdev);
	if (error == 0)
		return;
	for (mount_entry = mount; mount_entry < &mount[NMOUNT]; mount_entry++) {
		if (mount_entry->m_inodp == NULL || mount_entry->m_dev != rootdev)
			continue;
		if (mount_entry->m_write_error == 0)
			mount_entry->m_write_error = error;
		return;
	}
	printf("checkpoint error on dev %o: error %d\n", rootdev, error);
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

	shutdown_checkpoint_root();
	shutdown_report_write_errors();
}
