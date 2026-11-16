/*	$NetBSD: v7fs_datablock.c,v 1.5 2011/08/14 09:02:07 apb Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: v7fs_datablock.c,v 1.5 2011/08/14 09:02:07 apb Exp $");
#if defined _KERNEL_OPT
#include "opt_v7fs.h"
#endif

#include <sys/types.h>
#include <stddef.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/param.h>
#else
#include <stdio.h>
#include <string.h>
#include <errno.h>
#endif

#include "v7fs.h"
#include "v7fs_impl.h"
#include "v7fs_endian.h"
#include "v7fs_inode.h"
#include "v7fs_datablock.h"
#include "v7fs_superblock.h"

#ifdef V7FS_DATABLOCK_DEBUG
#define	DPRINTF(fmt, args...)	printf("%s: " fmt, __func__, ##args)
#else
#define	DPRINTF(fmt, args...)	((void)0)
#endif

static int v7fs_datablock_deallocate(struct v7fs_self *, v7fs_daddr_t);
static int v7fs_loop1(struct v7fs_self *, v7fs_daddr_t, size_t *,
    int (*)(struct v7fs_self *, void *, v7fs_daddr_t, size_t), void *);
static int v7fs_loop2(struct v7fs_self *, v7fs_daddr_t, size_t *,
    int (*)(struct v7fs_self *, void *, v7fs_daddr_t, size_t), void *);
static v7fs_daddr_t v7fs_link(struct v7fs_self *, v7fs_daddr_t, int);
static int v7fs_append_block(struct v7fs_self *, struct v7fs_inode *, size_t,
    bool *);
static int v7fs_rollback_block(struct v7fs_self *, struct v7fs_inode *, size_t,
    bool, bool *);

#ifdef V7FS_DATABLOCK_DEBUG
void daddr_map_dump(const struct v7fs_daddr_map *);
#else
#define	daddr_map_dump(x)	((void)0)
#endif

bool
datablock_number_sanity(const struct v7fs_self *fs, v7fs_daddr_t blk)
{
	const struct v7fs_superblock *sb = &fs->superblock;
	bool ok = (blk >= sb->datablock_start_sector) &&
	    (blk < sb->volume_size);

#ifdef V7FS_DATABLOCK_DEBUG
	if (!ok) {
		DPRINTF("Bad data block #%d\n", blk);
	}
#endif

	return ok;
}

int
v7fs_datablock_allocate(struct v7fs_self *fs, v7fs_daddr_t *block_number)
{
	struct v7fs_superblock *sb = &fs->superblock;
	v7fs_daddr_t blk;
	int error = 0;

	*block_number = 0;
	SUPERB_LOCK(fs);
	do {
		if (!sb->total_freeblock) {
			DPRINTF("free block exhausted!!!\n");
			SUPERB_UNLOCK(fs);
			return ENOSPC;
		}

		/* Get free block from superblock cache. */
		blk = sb->freeblock[--sb->nfreeblock];
		sb->total_freeblock--;
		sb->modified = 1;

		/* If nfreeblock is zero, it block is next freeblock link. */
		if (sb->nfreeblock == 0) {
			if ((error = v7fs_freeblock_update(fs, blk))) {
				DPRINTF("no freeblock!!!\n");
				sb->nfreeblock++;
				sb->total_freeblock++;
				SUPERB_UNLOCK(fs);
				return error;
			}
			/* This freeblock link is no longer required. */
			/* use as data block. */
		}
	} while (!datablock_number_sanity(fs, blk)); /* skip bogus block. */
	SUPERB_UNLOCK(fs);

	DPRINTF("Get freeblock %d\n", blk);
	/* Zero clear datablock. */
	void *buf;
	if (!(buf = scratch_read(fs, blk))) {
		error = v7fs_datablock_deallocate(fs, blk);
		return error != 0 ? error : EIO;
	}
	memset(buf, 0, V7FS_BSIZE);
	if (!fs->io.write(fs->io.cookie, buf, blk))
		error = EIO;
	scratch_free(fs, buf);
	if (error != 0) {
		int rollback_error = v7fs_datablock_deallocate(fs, blk);
		return rollback_error != 0 ? rollback_error : error;
	}

	*block_number = blk;

	return error;
}

static int
v7fs_datablock_deallocate(struct v7fs_self *fs, v7fs_daddr_t blk)
{
	struct v7fs_superblock *sb = &fs->superblock;
	void *buf;
	int error = 0;

	if (!datablock_number_sanity(fs, blk))
		return EIO;

	/* Add to in-core freelist. */
	SUPERB_LOCK(fs);
	if (sb->nfreeblock < V7FS_MAX_FREEBLOCK) {
		sb->freeblock[sb->nfreeblock++] = blk;
		sb->total_freeblock++;
		sb->modified = 1;
		DPRINTF("n_freeblock=%d\n", sb->total_freeblock);
		SUPERB_UNLOCK(fs);
		return 0;
	}

	/* No space to push. */
	/* Make this block to freeblock list.and current cache moved to this. */
	if (!(buf = scratch_read(fs, blk))) {
		SUPERB_UNLOCK(fs);
		return EIO;	/* Fatal */
	}

	struct v7fs_freeblock *fb = (struct v7fs_freeblock *)buf;
	fb->nfreeblock = V7FS_MAX_FREEBLOCK;
	int i;
	for (i = 0; i < V7FS_MAX_FREEBLOCK; i++)
		fb->freeblock[i] = V7FS_VAL32(fs, sb->freeblock[i]);

	if (!fs->io.write(fs->io.cookie, (uint8_t *)fb, blk)) {
		error =  EIO;	/* Fatal */
	} else {
		/* Link. on next allocate, this block is used as datablock, */
		/* and swap outed freeblock list is restored. */
		sb->freeblock[0] = blk;
		sb->nfreeblock = 1;
		sb->total_freeblock++;
		sb->modified = 1;
		DPRINTF("n_freeblock=%d\n", sb->total_freeblock);
	}
	SUPERB_UNLOCK(fs);
	scratch_free(fs, buf);

	return error;
}

int
v7fs_datablock_addr(size_t sz, struct v7fs_daddr_map *map)
{
#define	NIDX		V7FS_DADDR_PER_BLOCK
#define	DIRECT_SZ	(V7FS_NADDR_DIRECT * V7FS_BSIZE)
#define	IDX1_SZ		(NIDX * V7FS_BSIZE)
#define	IDX2_SZ		(NIDX * NIDX * V7FS_BSIZE)
#define	ROUND(x, a)	((((x) + ((a) - 1)) / (a)) * (a))
	uint64_t offset;
	const uint64_t maximum = ((uint64_t)V7FS_NADDR_DIRECT + NIDX +
	    (uint64_t)NIDX * NIDX + (uint64_t)NIDX * NIDX * NIDX) * V7FS_BSIZE;
	if ((uint64_t)sz > maximum)
		return ENOSPC;
	if (!sz) {
		map->level = 0;
		map->index[0] = 0;
		return 0;
	}

	offset = ((uint64_t)sz + V7FS_BSIZE - 1) / V7FS_BSIZE * V7FS_BSIZE;

	/* Direct */
	if (offset <= DIRECT_SZ) {
		map->level = 0;
		map->index[0] = (offset >> V7FS_BSHIFT) - 1;
		return 0;
	}
	/* Index 1 */
	offset -= DIRECT_SZ;

	if (offset <= IDX1_SZ) {
		map->level = 1;
		map->index[0] = (offset >> V7FS_BSHIFT) - 1;
		return 0;
	}
	offset -= IDX1_SZ;

	/* Index 2 */
	if (offset <= IDX2_SZ) {
		map->level = 2;
		map->index[0] = ROUND(offset, IDX1_SZ) / IDX1_SZ - 1;
		map->index[1] = ((offset - (map->index[0] * IDX1_SZ)) >>
		    V7FS_BSHIFT) - 1;
		return 0;
	}
	offset -= IDX2_SZ;

	/* Index 3 */
	map->level = 3;
	map->index[0] = ROUND(offset, IDX2_SZ) / IDX2_SZ - 1;
	offset -= map->index[0] * IDX2_SZ;
	map->index[1] = ROUND(offset, IDX1_SZ) / IDX1_SZ - 1;
	offset -= map->index[1] * IDX1_SZ;
	map->index[2] = (offset >> V7FS_BSHIFT) - 1;

	return map->index[0] >= NIDX || map->index[1] >= NIDX ||
	    map->index[2] >= NIDX ? ENOSPC : 0;
}

int
v7fs_datablock_foreach(struct v7fs_self *fs, struct v7fs_inode *p,
    int (*func)(struct v7fs_self *, void *, v7fs_daddr_t, size_t), void *ctx)
{
	size_t i;
	v7fs_daddr_t blk, blk2;
	size_t filesize;
	bool last;
	int ret;

	if (!(filesize = v7fs_inode_filesize(p)))
		return V7FS_ITERATOR_END;
#ifdef V7FS_DATABLOCK_DEBUG
	size_t sz = filesize;
#endif

	/* Direct */
	for (i = 0; i < V7FS_NADDR_DIRECT; i++, filesize -= V7FS_BSIZE) {
		blk = p->addr[i];
		if (!datablock_number_sanity(fs, blk)) {
			DPRINTF("inode#%d direct=%zu filesize=%zu\n",
			    p->inode_number, i, sz);
			return EIO;
		}

		last = filesize <= V7FS_BSIZE;
		if ((ret = func(fs, ctx, blk, last ? filesize : V7FS_BSIZE)))
			return ret;
		if (last)
			return V7FS_ITERATOR_END;
	}

	/* Index 1 */
	blk = p->addr[V7FS_NADDR_INDEX1];
	if (!datablock_number_sanity(fs, blk))
		return EIO;

	if ((ret = v7fs_loop1(fs, blk, &filesize, func, ctx)))
		return ret;

	/* Index 2 */
	blk = p->addr[V7FS_NADDR_INDEX2];
	if (!datablock_number_sanity(fs, blk))
		return EIO;

	if ((ret = v7fs_loop2(fs, blk, &filesize, func, ctx)))
		return ret;

	/* Index 3 */
	blk = p->addr[V7FS_NADDR_INDEX3];
	if (!datablock_number_sanity(fs, blk))
		return EIO;

	for (i = 0; i < V7FS_DADDR_PER_BLOCK; i++) {
		blk2 = v7fs_link(fs, blk, i);
		if (!datablock_number_sanity(fs, blk))
			return EIO;

		if ((ret = v7fs_loop2(fs, blk2, &filesize, func, ctx)))
			return ret;
	}

	return EFBIG;
}

static int
v7fs_loop2(struct v7fs_self *fs, v7fs_daddr_t listblk, size_t *filesize,
    int (*func)(struct v7fs_self *, void *, v7fs_daddr_t, size_t), void *ctx)
{
	v7fs_daddr_t blk;
	int ret;
	size_t j;

	for (j = 0; j < V7FS_DADDR_PER_BLOCK; j++) {
		blk = v7fs_link(fs, listblk, j);
		if (!datablock_number_sanity(fs, blk))
			return EIO;
		if ((ret = v7fs_loop1(fs, blk, filesize, func, ctx)))
			return ret;
	}

	return 0;
}

static int
v7fs_loop1(struct v7fs_self *fs, v7fs_daddr_t listblk, size_t *filesize,
    int (*func)(struct v7fs_self *, void *, v7fs_daddr_t, size_t), void *ctx)
{
	v7fs_daddr_t blk;
	bool last;
	int ret;
	size_t k;

	for (k = 0; k < V7FS_DADDR_PER_BLOCK; k++, *filesize -= V7FS_BSIZE) {
		blk = v7fs_link(fs, listblk, k);
		if (!datablock_number_sanity(fs, blk))
			return EIO;
		last = *filesize <= V7FS_BSIZE;
		if ((ret = func(fs, ctx, blk, last ? *filesize : V7FS_BSIZE)))
			return ret;
		if (last)
			return V7FS_ITERATOR_END;
	}

	return 0;
}

v7fs_daddr_t
v7fs_datablock_last(struct v7fs_self *fs, struct v7fs_inode *inode,
    v7fs_off_t ofs)
{
	struct v7fs_daddr_map map;
	v7fs_daddr_t blk = 0;
	v7fs_daddr_t *addr = inode->addr;

	/* Inquire last data block location. */
	if (v7fs_datablock_addr(ofs, &map) != 0)
		return 0;

	switch (map.level)
	{
	case 0: /*Direct */
		blk = inode->addr[map.index[0]];
		break;
	case 1: /*Index1 */
		blk = v7fs_link(fs, addr[V7FS_NADDR_INDEX1], map.index[0]);
		break;
	case 2: /*Index2 */
		blk = v7fs_link(fs, v7fs_link(fs,
		    addr[V7FS_NADDR_INDEX2], map.index[0]), map.index[1]);
		break;
	case 3: /*Index3 */
		blk = v7fs_link(fs, v7fs_link(fs, v7fs_link(fs,
		    addr[V7FS_NADDR_INDEX3], map.index[0]), map.index[1]),
		    map.index[2]);
		break;
	}

	return blk;
}

/* Read an indirect entry without assuming the scratch buffer is aligned. */
static int
v7fs_pointer_read(struct v7fs_self *fs, v7fs_daddr_t parent, unsigned index,
    v7fs_daddr_t *child)
{
	void *buffer;
	v7fs_daddr_t disk_value;

	if (!datablock_number_sanity(fs, parent) ||
	    index >= V7FS_DADDR_PER_BLOCK || !(buffer = scratch_read(fs, parent)))
		return EIO;
	memcpy(&disk_value, (uint8_t *)buffer + index * sizeof(disk_value),
	    sizeof(disk_value));
	scratch_free(fs, buffer);
	*child = V7FS_VAL32(fs, disk_value);
	if (*child != 0 && !datablock_number_sanity(fs, *child))
		return EIO;
	return 0;
}

/* Restore the old entry after a failed write before releasing its child. */
static int
v7fs_pointer_write(struct v7fs_self *fs, v7fs_daddr_t parent, unsigned index,
    v7fs_daddr_t expected, v7fs_daddr_t replacement, bool *restored)
{
	void *buffer;
	v7fs_daddr_t original, value;

	*restored = true;
	if (!datablock_number_sanity(fs, parent) ||
	    index >= V7FS_DADDR_PER_BLOCK || !(buffer = scratch_read(fs, parent)))
		return EIO;
	memcpy(&original, (uint8_t *)buffer + index * sizeof(original),
	    sizeof(original));
	if (V7FS_VAL32(fs, original) != expected) {
		scratch_free(fs, buffer);
		return EIO;
	}
	value = V7FS_VAL32(fs, replacement);
	memcpy((uint8_t *)buffer + index * sizeof(value), &value, sizeof(value));
	if (!fs->io.write(fs->io.cookie, buffer, parent)) {
		memcpy((uint8_t *)buffer + index * sizeof(original), &original,
		    sizeof(original));
		*restored = fs->io.write(fs->io.cookie, buffer, parent);
		scratch_free(fs, buffer);
		return EIO;
	}
	scratch_free(fs, buffer);
	return 0;
}

/* An index block can be released only when its other entries are empty. */
static int
v7fs_pointer_only(struct v7fs_self *fs, v7fs_daddr_t parent, unsigned index,
    v7fs_daddr_t expected)
{
	void *buffer;
	unsigned entry;
	int error = 0;

	if (!datablock_number_sanity(fs, parent) ||
	    index >= V7FS_DADDR_PER_BLOCK || !(buffer = scratch_read(fs, parent)))
		return EIO;
	for (entry = 0; entry < V7FS_DADDR_PER_BLOCK; entry++) {
		v7fs_daddr_t value;

		memcpy(&value, (uint8_t *)buffer + entry * sizeof(value), sizeof(value));
		value = V7FS_VAL32(fs, value);
		if (value != (entry == index ? expected : 0)) {
			error = EIO;
			break;
		}
	}
	scratch_free(fs, buffer);
	return error;
}

/* An older contraction can leave a freed block in an unused direct slot. */
static int
v7fs_block_is_free(struct v7fs_self *fs, v7fs_daddr_t target, bool *is_free)
{
	struct v7fs_superblock *sb = &fs->superblock;
	v7fs_daddr_t next, disk_block;
	uint32_t remaining;
	int index, count, error = 0;
	void *buffer;

	*is_free = false;
	SUPERB_LOCK(fs);
	count = sb->nfreeblock;
	if (count < 0 || count > V7FS_MAX_FREEBLOCK) {
		error = EIO;
		goto out;
	}
	for (index = 0; index < count; index++) {
		if (sb->freeblock[index] == target) {
			*is_free = true;
			goto out;
		}
	}
	next = count == 0 ? 0 : sb->freeblock[0];
	remaining = sb->volume_size;
	while (next != 0) {
		int16_t disk_count;

		if (remaining-- == 0 || !datablock_number_sanity(fs, next) ||
		    !(buffer = scratch_read(fs, next))) {
			error = EIO;
			goto out;
		}
		memcpy(&disk_count, buffer, sizeof(disk_count));
		count = V7FS_VAL16(fs, disk_count);
		if (count <= 0 || count > V7FS_MAX_FREEBLOCK) {
			scratch_free(fs, buffer);
			error = EIO;
			goto out;
		}
		next = 0;
		for (index = 0; index < count; index++) {
			memcpy(&disk_block, (uint8_t *)buffer +
			    offsetof(struct v7fs_freeblock, freeblock) +
			    index * sizeof(disk_block), sizeof(disk_block));
			disk_block = V7FS_VAL32(fs, disk_block);
			if (index == 0)
				next = disk_block;
			if (disk_block == target)
				*is_free = true;
		}
		scratch_free(fs, buffer);
		if (*is_free)
			goto out;
	}
out:
	SUPERB_UNLOCK(fs);
	return error;
}

/* Construct missing index blocks privately before publishing the branch. */
static int
v7fs_append_block(struct v7fs_self *fs, struct v7fs_inode *inode, size_t block,
    bool *uncertain)
{
	struct v7fs_daddr_map map;
	v7fs_daddr_t parent = 0, child = 0, allocated[4];
	unsigned count = 0;
	int depth, index, error, cleanup_error = 0;
	bool restored = true;

	*uncertain = false;
	if (v7fs_datablock_addr(block * V7FS_BSIZE + 1, &map) != 0)
		return EFBIG;
	if (map.level == 0) {
		if (inode->addr[map.index[0]] != 0)
			return EIO;
		error = v7fs_datablock_allocate(fs, &child);
		if (error == 0)
			inode->addr[map.index[0]] = child;
		return error;
	}
	parent = inode->addr[V7FS_NADDR_INDEX1 + map.level - 1];
	depth = -1;
	if (parent != 0) {
		for (depth = 0; depth < map.level; depth++) {
			error = v7fs_pointer_read(fs, parent, map.index[depth], &child);
			if (error != 0)
				return error;
			if (child == 0)
				break;
			if (depth == map.level - 1)
				return EIO;
			parent = child;
		}
	}
	error = v7fs_datablock_allocate(fs, &child);
	if (error != 0)
		return error;
	allocated[count++] = child;
	for (index = map.level - 1; index > depth; index--) {
		v7fs_daddr_t new_parent;

		error = v7fs_datablock_allocate(fs, &new_parent);
		if (error != 0)
			goto cleanup;
		allocated[count++] = new_parent;
		error = v7fs_pointer_write(fs, new_parent, map.index[index], 0,
		    child, &restored);
		if (error != 0)
			goto cleanup;
		child = new_parent;
	}
	if (depth < 0) {
		inode->addr[V7FS_NADDR_INDEX1 + map.level - 1] = child;
		return 0;
	}
	error = v7fs_pointer_write(fs, parent, map.index[depth], 0, child,
	    &restored);
	if (error == 0)
		return 0;
	if (!restored) {
		*uncertain = true;
		return error;
	}
cleanup:
	while (count != 0) {
		int result = v7fs_datablock_deallocate(fs, allocated[--count]);
		if (result != 0)
			cleanup_error = result;
	}
	if (cleanup_error != 0)
		*uncertain = true;
	return cleanup_error != 0 ? cleanup_error : error;
}

/* Detach the last appended block and any newly empty index blocks. */
static int
v7fs_rollback_block(struct v7fs_self *fs, struct v7fs_inode *inode, size_t block,
    bool persist_inode, bool *detached)
{
	struct v7fs_daddr_map map;
	v7fs_daddr_t nodes[3], child;
	int depth, prune, error;
	bool restored;

	*detached = false;
	if (v7fs_datablock_addr(block * V7FS_BSIZE + 1, &map) != 0)
		return EIO;
	if (map.level == 0) {
		child = inode->addr[map.index[0]];
		inode->addr[map.index[0]] = 0;
		if (persist_inode && (error = v7fs_inode_writeback(fs, inode)) != 0) {
			int restore_error;
			inode->addr[map.index[0]] = child;
			restore_error = v7fs_inode_writeback(fs, inode);
			return restore_error != 0 ? restore_error : error;
		}
		*detached = true;
		return v7fs_datablock_deallocate(fs, child);
	}
	nodes[0] = inode->addr[V7FS_NADDR_INDEX1 + map.level - 1];
	for (depth = 0; depth < map.level; depth++) {
		error = v7fs_pointer_read(fs, nodes[depth], map.index[depth], &child);
		if (error != 0 || child == 0)
			return EIO;
		if (depth + 1 < map.level)
			nodes[depth + 1] = child;
	}
	prune = map.level;
	while (prune > 0 && map.index[prune - 1] == 0)
		prune--;
	for (depth = prune; depth < map.level; depth++) {
		v7fs_daddr_t expected = depth + 1 == map.level ? child : nodes[depth + 1];
		if ((error = v7fs_pointer_only(fs, nodes[depth],
		    map.index[depth], expected)) != 0)
			return error;
	}
	if (prune == 0) {
		inode->addr[V7FS_NADDR_INDEX1 + map.level - 1] = 0;
		if (persist_inode && (error = v7fs_inode_writeback(fs, inode)) != 0) {
			int restore_error;
			inode->addr[V7FS_NADDR_INDEX1 + map.level - 1] = nodes[0];
			restore_error = v7fs_inode_writeback(fs, inode);
			return restore_error != 0 ? restore_error : error;
		}
	} else {
		v7fs_daddr_t detached_block = prune == map.level ? child : nodes[prune];
		error = v7fs_pointer_write(fs, nodes[prune - 1],
		    map.index[prune - 1], detached_block, 0, &restored);
		if (error != 0) {
			if (!restored)
				*detached = true;
			return error;
		}
	}
	*detached = true;
	if ((error = v7fs_datablock_deallocate(fs, child)) != 0)
		return error;
	for (depth = map.level - 1; depth >= prune; depth--)
		if ((error = v7fs_datablock_deallocate(fs, nodes[depth])) != 0)
			return error;
	return 0;
}

int
v7fs_datablock_expand(struct v7fs_self *fs, struct v7fs_inode *inode, size_t sz)
{
	struct v7fs_inode backup = *inode;
	uint64_t new_size;
	struct v7fs_daddr_map final_map;
	size_t old_blocks, new_blocks, completed, block;
	unsigned stale_slots = 0;
	int error, rollback_error;
	bool uncertain, detached;

	if (sz > UINT32_MAX - inode->filesize)
		return EFBIG;
	new_size = (uint64_t)inode->filesize + sz;
	if (new_size != 0 && v7fs_datablock_addr((size_t)new_size, &final_map) != 0)
		return EFBIG;
	old_blocks = ((uint64_t)inode->filesize + V7FS_BSIZE - 1) / V7FS_BSIZE;
	new_blocks = (new_size + V7FS_BSIZE - 1) / V7FS_BSIZE;
	/* Allocation can reuse a free block named by any unused direct slot. */
	for (block = old_blocks; new_blocks > old_blocks &&
	    block < V7FS_NADDR_DIRECT; block++) {
		bool is_free;

		if (inode->addr[block] == 0)
			continue;
		error = v7fs_block_is_free(fs, inode->addr[block], &is_free);
		if (error != 0)
			return error;
		if (!is_free) {
			if (block < new_blocks)
				return EIO;
			continue;
		}
		stale_slots |= 1u << block;
	}
	for (block = old_blocks; block < V7FS_NADDR_DIRECT; block++)
		if ((stale_slots & (1u << block)) != 0)
			inode->addr[block] = 0;
	for (completed = old_blocks; completed < new_blocks; completed++) {
		error = v7fs_append_block(fs, inode, completed, &uncertain);
		if (error != 0 && uncertain)
			return error;
		if (error != 0)
			goto rollback;
	}
	inode->filesize = (v7fs_off_t)new_size;
	error = v7fs_inode_writeback(fs, inode);
	if (error == 0)
		return 0;
	/* A failed write may have changed the inode sector before returning. */
	rollback_error = v7fs_inode_writeback(fs, &backup);
	if (rollback_error != 0) {
		if (new_blocks == old_blocks)
			*inode = backup;
		return rollback_error;
	}
rollback:
	while (completed > old_blocks) {
		rollback_error = v7fs_rollback_block(fs, inode, --completed, false,
		    &detached);
		if (rollback_error != 0)
			return rollback_error;
	}
	*inode = backup;
	return error;
}

static v7fs_daddr_t
v7fs_link(struct v7fs_self *fs, v7fs_daddr_t listblk, int n)
{
	v7fs_daddr_t *list;
	v7fs_daddr_t blk;
	void *buf;

	if (!datablock_number_sanity(fs, listblk))
		return 0;
	if (!(buf = scratch_read(fs, listblk)))
		return 0;
	list = (v7fs_daddr_t *)buf;
	blk = V7FS_VAL32(fs, list[n]);
	scratch_free(fs, buf);

	if (!datablock_number_sanity(fs, blk))
		return 0;

	return blk;
}

int
v7fs_datablock_contract_status(struct v7fs_self *fs, struct v7fs_inode *inode,
    size_t sz, bool *retryable)
{
	struct v7fs_inode previous;
	uint64_t new_size;
	size_t old_blocks, new_blocks, block;
	int error, restore_error;
	bool detached;

	*retryable = false;
	if (sz > inode->filesize)
		return EINVAL;
	new_size = inode->filesize - sz;
	old_blocks = ((uint64_t)inode->filesize + V7FS_BSIZE - 1) / V7FS_BSIZE;
	new_blocks = (new_size + V7FS_BSIZE - 1) / V7FS_BSIZE;
	for (block = old_blocks; block > new_blocks; block--) {
		previous = *inode;
		inode->filesize = (v7fs_off_t)((block - 1) * V7FS_BSIZE);
		if (inode->filesize < new_size)
			inode->filesize = (v7fs_off_t)new_size;
		error = v7fs_inode_writeback(fs, inode);
		if (error != 0) {
			*inode = previous;
			restore_error = v7fs_inode_writeback(fs, inode);
			*retryable = restore_error == 0;
			return restore_error != 0 ? restore_error : error;
		}
		error = v7fs_rollback_block(fs, inode, block - 1, true, &detached);
		if (error != 0 && !detached) {
			*inode = previous;
			restore_error = v7fs_inode_writeback(fs, inode);
			*retryable = restore_error == 0;
			return restore_error != 0 ? restore_error : error;
		}
		if (error != 0)
			return error;
	}
	if (inode->filesize == new_size) {
		*retryable = true;
		return 0;
	}
	previous = *inode;
	inode->filesize = (v7fs_off_t)new_size;
	error = v7fs_inode_writeback(fs, inode);
	if (error != 0) {
		*inode = previous;
		restore_error = v7fs_inode_writeback(fs, inode);
		*retryable = restore_error == 0;
		return restore_error != 0 ? restore_error : error;
	}
	*retryable = true;
	return 0;
}

int
v7fs_datablock_contract(struct v7fs_self *fs, struct v7fs_inode *inode,
    size_t sz)
{
	bool retryable;

	return v7fs_datablock_contract_status(fs, inode, sz, &retryable);
}

int
v7fs_datablock_size_change(struct v7fs_self *fs, size_t newsz,
    struct v7fs_inode *inode)
{
	size_t old_size = v7fs_inode_filesize(inode);

	if (newsz > UINT32_MAX)
		return EFBIG;
	if (newsz > old_size)
		return v7fs_datablock_expand(fs, inode, newsz - old_size);
	if (newsz < old_size)
		return v7fs_datablock_contract(fs, inode, old_size - newsz);
	return 0;
}

#ifdef V7FS_DATABLOCK_DEBUG
void
daddr_map_dump(const struct v7fs_daddr_map *map)
{

	DPRINTF("level %d ", map->level);
	int m, n = !map->level ? 1 : map->level;
	for (m = 0; m < n; m++)
		printf("[%d]", map->index[m]);
	printf("\n");
}
#endif
