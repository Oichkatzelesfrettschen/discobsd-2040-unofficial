/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Directory and status shims for the devname() contract. The test compiles
 * lib/libc/gen/devname.c and this file against the port's headers, with
 * opendir, readdir, closedir, stat and perror renamed to these functions.
 * Like the libc readdir(), the shim returns an entry stored inside the DIR,
 * and closedir() frees the DIR, so a caller that keeps the entry's name
 * past the close reads freed memory, which AddressSanitizer reports.
 */
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devname_shim.h"

#define	SHIM_NODES	8

struct shim_node {
	char	name[MAXNAMLEN + 1];
	u_int	mode;
	dev_t	rdev;
};

static struct shim_node nodes[SHIM_NODES];
static int node_count;
static int next_node;
static int open_dirs;
static int refuse_open;

void
devname_shim_reset(int refuse)
{
	memset(nodes, 0, sizeof(nodes));
	node_count = 0;
	next_node = 0;
	open_dirs = 0;
	refuse_open = refuse;
}

void
devname_shim_add(const char *name, int character, int rdev)
{
	struct shim_node *node;

	if (node_count == SHIM_NODES || strlen(name) > MAXNAMLEN)
		abort();
	node = &nodes[node_count++];
	strcpy(node->name, name);
	node->mode = (character ? S_IFCHR : S_IFBLK) | 0620;
	node->rdev = rdev;
}

int
devname_shim_type(int character)
{
	return (character ? S_IFCHR : S_IFBLK);
}

int
devname_shim_open_dirs(void)
{
	return (open_dirs);
}

DIR *
opendir(const char *path)
{
	DIR *dir;

	if (refuse_open || strcmp(path, "/dev") != 0)
		return (NULL);
	dir = calloc(1, sizeof(*dir));
	if (dir == NULL)
		abort();
	next_node = 0;
	open_dirs++;
	return (dir);
}

struct direct *
readdir(DIR *dir)
{
	struct direct *entry = (struct direct *)(void *)dir->dd_buf;

	if (next_node == node_count)
		return (NULL);
	memset(entry, 0, sizeof(*entry));
	entry->d_ino = (ino_t)(next_node + 2);
	entry->d_reclen = sizeof(*entry);
	entry->d_namlen = (u_short)strlen(nodes[next_node].name);
	strcpy(entry->d_name, nodes[next_node].name);
	next_node++;
	return (entry);
}

int
closedir(DIR *dir)
{
	open_dirs--;
	free(dir);
	return (0);
}

int
stat(const char *path, struct stat *status)
{
	int i;

	if (strncmp(path, "/dev/", 5) != 0)
		return (-1);
	for (i = 0; i < node_count; i++) {
		if (strcmp(path + 5, nodes[i].name) == 0) {
			memset(status, 0, sizeof(*status));
			status->st_mode = nodes[i].mode;
			status->st_rdev = nodes[i].rdev;
			return (0);
		}
	}
	return (-1);
}

void
perror(const char *message)
{
	(void)message;
}
