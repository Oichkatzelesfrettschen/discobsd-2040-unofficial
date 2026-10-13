#include <sys/param.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned close_calls;
static unsigned read_calls;
static unsigned open_calls;
static int crosses_device;
static int at_root;
static int empty_parent;

static void
set_status(struct stat *status, ino_t inode, dev_t device)
{
	memset(status, 0, sizeof(*status));
	status->st_ino = inode;
	status->st_dev = device;
	status->st_mode = S_IFDIR | 0755;
}

int
test_stat(const char *path, struct stat *status)
{
	if (strcmp(path, "/") == 0 || at_root)
		set_status(status, 1, 1);
	else
		set_status(status, 2, crosses_device ? 2 : 1);
	return 0;
}

int
test_fstat(int descriptor, struct stat *status)
{
	if (descriptor != 7)
		return -1;
	set_status(status, 1, 1);
	return 0;
}

int
test_lstat(const char *path, struct stat *status)
{
	if (strcmp(path, "./../leaf") != 0)
		return -1;
	set_status(status, 2, 2);
	return 0;
}

DIR *
test_opendir(const char *path)
{
	DIR *directory;

	++open_calls;
	if (strcmp(path, "./../") != 0)
		return NULL;
	directory = malloc(sizeof(*directory));
	if (directory != NULL) {
		memset(directory, 0, sizeof(*directory));
		directory->dd_fd = 7;
	}
	return directory;
}

struct direct *
test_readdir(DIR *directory)
{
	struct direct *entry;

	if (read_calls++ != 0 || empty_parent)
		return NULL;
	entry = (struct direct *)directory->dd_buf;
	entry->d_ino = 2;
	entry->d_namlen = 4;
	strcpy(entry->d_name, "leaf");
	return entry;
}

int
test_closedir(DIR *directory)
{
	++close_calls;
	memset(directory->dd_buf, 0, sizeof(directory->dd_buf));
	free(directory);
	return 0;
}

static int
check_path(int cross_device, int root_directory, int empty_directory)
{
	char pathname[MAXPATHLEN];
	char *result;

	crosses_device = cross_device;
	at_root = root_directory;
	empty_parent = empty_directory;
	close_calls = 0;
	read_calls = 0;
	open_calls = 0;
	result = discobsd_getwd(pathname);
	if (empty_directory) {
		if (result != NULL || close_calls != 1 ||
		    strcmp(pathname, "getwd: read error in ..") != 0)
			return 1;
	} else if (result != pathname ||
	    strcmp(pathname, root_directory ? "/" : "/leaf") != 0 ||
	    close_calls != (root_directory ? 0U : 1U))
		return 1;
	return open_calls != close_calls;
}

int
main(void)
{
	if (check_path(0, 0, 0) || check_path(1, 0, 0) ||
	    check_path(0, 1, 0) || check_path(0, 0, 1)) {
		static const char message[] = "FAIL getwd borrowed entry lifetime\n";
		(void)write(STDERR_FILENO, message, sizeof(message) - 1);
		return 1;
	}
	return 0;
}
