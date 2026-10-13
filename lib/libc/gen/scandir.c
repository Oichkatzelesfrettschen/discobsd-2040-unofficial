/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Scan a directory, copy selected entries and sort them with a typed
 * comparator.  The pointer vector grows from selected entries rather than
 * directory file size, because directory metadata does not bound selection.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
compare_entries(struct direct *left_entry, struct direct *right_entry,
    int (*comparison)(const struct direct **, const struct direct **))
{
	const struct direct *left_argument = left_entry;
	const struct direct *right_argument = right_entry;

	return (*comparison)(&left_argument, &right_argument);
}

static void
exchange_entries(struct direct **left, struct direct **right)
{
	struct direct *temporary;

	temporary = *left;
	*left = *right;
	*right = temporary;
}

static void
sift_entries(struct direct **names, size_t root, size_t end,
    int (*comparison)(const struct direct **, const struct direct **))
{
	size_t child;

	if (end == 0)
		return;
	while (root <= (end - 1) / 2) {
		child = root * 2 + 1;
		if (child < end &&
		    compare_entries(names[child], names[child + 1], comparison) < 0)
			++child;
		if (compare_entries(names[root], names[child], comparison) >= 0)
			return;
		exchange_entries(&names[root], &names[child]);
		root = child;
	}
}

static void
sort_entries(struct direct **names, size_t entry_count,
    int (*comparison)(const struct direct **, const struct direct **))
{
	size_t end;
	size_t start;

	if (entry_count < 2)
		return;
	start = (entry_count - 2) / 2;
	for (;;) {
		sift_entries(names, start, entry_count - 1, comparison);
		if (start == 0)
			break;
		--start;
	}
	for (end = entry_count - 1; end != 0; --end) {
		exchange_entries(&names[0], &names[end]);
		sift_entries(names, 0, end - 1, comparison);
	}
}

int
scandir(const char *dirname, struct direct ***namelist,
    int (*select_entry)(const struct direct *),
    int (*comparison)(const struct direct **, const struct direct **))
{
	struct direct *entry;
	struct direct *entry_copy;
	struct direct **grown_names;
	struct direct **names = NULL;
	struct stat status;
	DIR *directory;
	size_t capacity = 0;
	size_t entry_count = 0;
	size_t name_index;
	size_t new_capacity;
	size_t record_size;
	int saved_errno;

	directory = opendir(dirname);
	if (directory == NULL)
		return -1;
	if (fstat(directory->dd_fd, &status) < 0)
		goto fail;
	if (!S_ISDIR(status.st_mode)) {
		errno = ENOTDIR;
		goto fail;
	}

	for (;;) {
		errno = 0;
		entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0)
				goto fail;
			break;
		}
		if (select_entry != NULL && !(*select_entry)(entry))
			continue;
		if (entry_count == (size_t)INT_MAX) {
			errno = EOVERFLOW;
			goto fail;
		}

		if (entry_count == capacity) {
			if (capacity == 0)
				new_capacity = 4;
			else {
				new_capacity = capacity * 2;
				if (new_capacity > (size_t)INT_MAX)
					new_capacity = (size_t)INT_MAX;
			}
			if (new_capacity > (size_t)-1 / sizeof(*names)) {
				errno = ENOMEM;
				goto fail;
			}
			if (names == NULL)
				grown_names = malloc(new_capacity * sizeof(*names));
			else
				grown_names = realloc(names,
				    new_capacity * sizeof(*names));
			if (grown_names == NULL)
				goto fail;
			names = grown_names;
			capacity = new_capacity;
		}

		record_size = DIRSIZ(entry);
		entry_copy = malloc(record_size);
		if (entry_copy == NULL)
			goto fail;
		entry_copy->d_ino = entry->d_ino;
		entry_copy->d_reclen = (u_short)record_size;
		entry_copy->d_namlen = entry->d_namlen;
		for (name_index = 0; name_index <= entry->d_namlen;
		    ++name_index)
			entry_copy->d_name[name_index] = entry->d_name[name_index];
		names[entry_count++] = entry_copy;
	}

	closedir(directory);
	if (comparison != NULL)
		sort_entries(names, entry_count, comparison);
	*namelist = names;
	return (int)entry_count;

fail:
	saved_errno = errno;
	while (entry_count != 0)
		free(names[--entry_count]);
	if (names != NULL)
		free(names);
	closedir(directory);
	errno = saved_errno;
	return -1;
}

/*
 * The tree has one fixed bytewise locale.  Preserve errno as POSIX requires
 * for a successful alphasort call; locale-aware strcoll remains unavailable.
 */
int
alphasort(const struct direct **left, const struct direct **right)
{
	int comparison;
	int saved_errno;

	saved_errno = errno;
	comparison = strcmp((*left)->d_name, (*right)->d_name);
	errno = saved_errno;
	return comparison;
}
