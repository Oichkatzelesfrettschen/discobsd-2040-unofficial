/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * lexeme_pick.c: the file picker (SPEC.md, Modern modes). It lists one
 * directory, subdirectories first and each marked with '/', into a fixed
 * name pool; a directory with more entries than the pool holds lists its
 * first PICK_MAX names and says so on the title line. Directories are read
 * through <dirent.h> on a host build and through the 4.3BSD <sys/dir.h>
 * interface on DiscoBSD.
 */

#include "lexeme.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef TERMIOS
#include <dirent.h>
#define PICK_DIRENT	struct dirent
#else
#include <sys/dir.h>
#define PICK_DIRENT	struct direct
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PICK_MAX	160
#define PICK_POOL	3072

static char pool[PICK_POOL];
static uint16_t name_at[PICK_MAX];
static uint8_t is_dir[PICK_MAX];
static int nent, sel, first, truncated;
static char dirpath[WORD_PATH_MAX];

static int
entry_cmp(int a, int b)
{
	if (is_dir[a] != is_dir[b])
		return is_dir[b] - is_dir[a];
	return strcmp(pool + name_at[a], pool + name_at[b]);
}

static void
join(char *out, size_t sz, const char *dir, const char *name)
{
	size_t n = strlen(dir);

	snprintf(out, sz, "%s%s%s", dir, n > 0 && dir[n - 1] == '/' ? "" : "/",
	    name);
}

int
pick_open(const char *dir)
{
	DIR *d;
	PICK_DIRENT *e;
	size_t used = 0;
	int i, j;

	if (dir == NULL || dir[0] == '\0')
		dir = ".";
	if (strlen(dir) >= sizeof(dirpath))
		return -1;
	d = opendir(dir);
	if (d == NULL)
		return -1;
	snprintf(dirpath, sizeof(dirpath), "%s", dir);
	nent = 0;
	truncated = 0;
	while ((e = readdir(d)) != NULL) {
		char full[WORD_PATH_MAX * 2];
		struct stat st;
		size_t len = strlen(e->d_name);

		if (strcmp(e->d_name, ".") == 0)
			continue;
		if (nent == PICK_MAX || used + len + 1 > sizeof(pool)) {
			truncated = 1;
			break;
		}
		join(full, sizeof(full), dirpath, e->d_name);
		is_dir[nent] = stat(full, &st) == 0 &&
		    (st.st_mode & S_IFMT) == S_IFDIR;
		memcpy(pool + used, e->d_name, len + 1);
		name_at[nent++] = (uint16_t)used;
		used += len + 1;
	}
	closedir(d);
	/* Insertion sort over the index pairs: the list is short. */
	for (i = 1; i < nent; i++)
		for (j = i; j > 0 && entry_cmp(j - 1, j) > 0; j--) {
			uint16_t t = name_at[j];
			uint8_t td = is_dir[j];

			name_at[j] = name_at[j - 1];
			is_dir[j] = is_dir[j - 1];
			name_at[j - 1] = t;
			is_dir[j - 1] = td;
		}
	sel = 0;
	first = 0;
	return 0;
}

void
pick_draw(int top, int rows)
{
	char line[WORD_PATH_MAX + 16];
	int r, i;

	snprintf(line, sizeof(line), "%s%s", dirpath, truncated ? " (first entries)" : "");
	scr_fill(top, 0, scr_cols, A_REV | A_CLS(C_TITLE));
	scr_puts(top, 0, line, A_REV | A_CLS(C_TITLE), scr_cols);
	rows--;
	if (sel < first)
		first = sel;
	if (sel >= first + rows)
		first = sel - rows + 1;
	for (r = 0; r < rows; r++) {
		int attr;

		i = first + r;
		scr_fill(top + 1 + r, 0, scr_cols, 0);
		if (i >= nent)
			continue;
		attr = i == sel ? A_REV | A_CLS(C_SEL) : 0;
		snprintf(line, sizeof(line), " %s%s", pool + name_at[i],
		    is_dir[i] ? "/" : "");
		scr_fill(top + 1 + r, 0, scr_cols, attr);
		scr_puts(top + 1 + r, 0, line, attr | (is_dir[i] ? A_BOLD : 0),
		    scr_cols);
	}
}

static int
descend(const char *name)
{
	char next[WORD_PATH_MAX];
	size_t n;

	if (strcmp(name, "..") == 0) {
		snprintf(next, sizeof(next), "%s", dirpath);
		n = strlen(next);
		while (n > 1 && next[n - 1] == '/')
			next[--n] = '\0';
		if (strcmp(next, ".") == 0 || strcmp(next, "..") == 0 ||
		    (n >= 3 && strcmp(next + n - 3, "/..") == 0))
			join(next, sizeof(next), dirpath, "..");
		else {
			char *s = strrchr(next, '/');

			if (s == NULL)
				snprintf(next, sizeof(next), ".");
			else if (s == next)
				next[1] = '\0';
			else
				*s = '\0';
		}
	} else {
		if (strlen(dirpath) + strlen(name) + 2 > sizeof(next))
			return -1;
		join(next, sizeof(next), dirpath, name);
	}
	return pick_open(next);
}

int
pick_key(int k, char *out, size_t outsz)
{
	int i;

	switch (k) {
	case K_ESC:
		return -1;
	case K_UP:
	case 'k':
		if (sel > 0)
			sel--;
		return 0;
	case K_DOWN:
	case 'j':
		if (sel + 1 < nent)
			sel++;
		return 0;
	case K_PGUP:
		sel = sel > 10 ? sel - 10 : 0;
		return 0;
	case K_PGDN:
		sel = sel + 10 < nent ? sel + 10 : (nent > 0 ? nent - 1 : 0);
		return 0;
	case K_BS:
	case K_LEFT:
		descend("..");
		return 0;
	case K_ENTER:
	case K_RIGHT:
		if (nent == 0)
			return 0;
		if (is_dir[sel]) {
			descend(pool + name_at[sel]);
			return 0;
		}
		if (strlen(dirpath) + strlen(pool + name_at[sel]) + 2 > outsz)
			return 0;
		if (strcmp(dirpath, ".") == 0)
			snprintf(out, outsz, "%s", pool + name_at[sel]);
		else
			join(out, outsz, dirpath, pool + name_at[sel]);
		return 1;
	default:
		if (k > ' ' && k < 0x7f && k != 'j' && k != 'k') {
			for (i = 1; i <= nent; i++) {
				int at = (sel + i) % nent;

				if (tolower((unsigned char)pool[name_at[at]]) ==
				    tolower(k)) {
					sel = at;
					break;
				}
			}
		}
		return 0;
	}
}
