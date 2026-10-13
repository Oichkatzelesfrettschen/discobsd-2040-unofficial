#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static DIR allocated_directory;
_Alignas(struct direct) static unsigned char directory_blocks[2][DIRBLKSIZ];
static off_t file_position;
static unsigned close_calls;
static unsigned free_calls;
static unsigned lseek_calls;
static unsigned malloc_calls;
static unsigned open_calls;
static unsigned read_calls;
static size_t read_length;
static int fail_lseek;
static int fail_malloc;
static int fail_open;
static int fail_read;
static int fail_fstat;
static int fail_fcntl;
static int close_error;
static int free_error;
static int descriptor_active;
static int descriptor_flags;
static mode_t opened_mode;
static unsigned fstat_calls;
static unsigned fcntl_calls;

enum operation {
	OPERATION_OPEN,
	OPERATION_FSTAT,
	OPERATION_FCNTL,
	OPERATION_MALLOC
};
static enum operation operation_trace[8];
static unsigned operation_count;

static void
record_operation(enum operation operation)
{
	if (operation_count < sizeof(operation_trace) / sizeof(operation_trace[0]))
		operation_trace[operation_count] = operation;
	++operation_count;
}

int dirent_test_errno;

static int
bytes_equal(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		++left;
		++right;
	}
	return *left == *right;
}

static void
zero_bytes(void *buffer, size_t length)
{
	unsigned char *bytes = buffer;

	while (length != 0) {
		*bytes++ = 0;
		--length;
	}
}

static void
copy_bytes(void *destination, const void *source, size_t length)
{
	unsigned char *output = destination;
	const unsigned char *input = source;

	while (length != 0) {
		*output++ = *input++;
		--length;
	}
}

static void
place_entry(unsigned block_index, unsigned offset, ino_t inode,
    unsigned short record_length, const char *name)
{
	struct direct *entry;
	unsigned short name_length = 0;

	entry = (struct direct *)(directory_blocks[block_index] + offset);
	while (name[name_length] != '\0')
		++name_length;
	entry->d_ino = inode;
	entry->d_reclen = record_length;
	entry->d_namlen = name_length;
	copy_bytes(entry->d_name, name, (size_t)name_length + 1);
}

static void
reset_fixture(void)
{
	zero_bytes(&allocated_directory, sizeof(allocated_directory));
	zero_bytes(directory_blocks, sizeof(directory_blocks));
	file_position = 0;
	close_calls = 0;
	free_calls = 0;
	lseek_calls = 0;
	malloc_calls = 0;
	open_calls = 0;
	read_calls = 0;
	read_length = DIRBLKSIZ;
	fail_lseek = 0;
	fail_malloc = 0;
	fail_open = 0;
	fail_read = 0;
	fail_fstat = 0;
	fail_fcntl = 0;
	close_error = 0;
	free_error = 0;
	descriptor_active = 0;
	descriptor_flags = 0;
	opened_mode = S_IFDIR | 0755;
	fstat_calls = 0;
	fcntl_calls = 0;
	operation_count = 0;
	dirent_test_errno = 0;

	place_entry(0, 0, 11, 12, "a");
	place_entry(0, 12, 0, 12, "");
	place_entry(0, 24, 22, DIRBLKSIZ - 24, "b");
	place_entry(1, 0, 33, DIRBLKSIZ, "c");
}

static int
check(int condition)
{
	return condition ? 0 : 1;
}

int
test_open(const char *path, int flags, ...)
{
	++open_calls;
	record_operation(OPERATION_OPEN);
	if (!bytes_equal(path, "fixture") || flags != O_RDONLY || fail_open) {
		dirent_test_errno = ENOENT;
		return -1;
	}
	file_position = 0;
	descriptor_active = 1;
	return 7;
}

int
test_close(int descriptor)
{
	++close_calls;
	descriptor_active = 0;
	descriptor_flags = 0;
	dirent_test_errno = close_error != 0 ? close_error : EBUSY;
	return descriptor == 7 && close_error == 0 ? 0 : -1;
}

int
test_fstat(int descriptor, struct stat *status)
{
	++fstat_calls;
	record_operation(OPERATION_FSTAT);
	if (descriptor != 7 || !descriptor_active || fail_fstat) {
		dirent_test_errno = EIO;
		return -1;
	}
	zero_bytes(status, sizeof(*status));
	status->st_mode = opened_mode;
	return 0;
}

int
test_fcntl(int descriptor, int command, ...)
{
	va_list arguments;
	int flags;

	++fcntl_calls;
	record_operation(OPERATION_FCNTL);
	if (descriptor != 7 || !descriptor_active || fail_fcntl) {
		dirent_test_errno = EBADF;
		return -1;
	}
	if (command == F_GETFD)
		return descriptor_flags;
	if (command != F_SETFD) {
		dirent_test_errno = EINVAL;
		return -1;
	}
	va_start(arguments, command);
	flags = va_arg(arguments, int);
	va_end(arguments);
	descriptor_flags = flags & FD_CLOEXEC;
	return 0;
}

void *
test_malloc(size_t size)
{
	++malloc_calls;
	record_operation(OPERATION_MALLOC);
	if (fail_malloc || size != sizeof(DIR)) {
		dirent_test_errno = ENOMEM;
		return NULL;
	}
	return &allocated_directory;
}

void
test_free(void *allocation)
{
	if (allocation == &allocated_directory)
		++free_calls;
	if (free_error != 0)
		dirent_test_errno = free_error;
}

ssize_t
test_read(int descriptor, void *buffer, size_t length)
{
	unsigned block_index;

	++read_calls;
	if (descriptor != 7 || length != DIRBLKSIZ || fail_read) {
		dirent_test_errno = EIO;
		return -1;
	}
	block_index = (unsigned)(file_position / DIRBLKSIZ);
	if (block_index >= 2)
		return 0;
	copy_bytes(buffer, directory_blocks[block_index], read_length);
	file_position += (off_t)read_length;
	return (ssize_t)read_length;
}

off_t
test_lseek(int descriptor, off_t offset, int whence)
{
	++lseek_calls;
	if (descriptor != 7 || whence != SEEK_SET || fail_lseek) {
		dirent_test_errno = EIO;
		return -1;
	}
	file_position = offset;
	return offset;
}

static int
test_stream_positions(void)
{
	struct direct *entry;
	DIR *directory;
	int failures = 0;

	reset_fixture();
	directory = discobsd_opendir("fixture");
	failures += check(directory == &allocated_directory);
	failures += check(open_calls == 1 && malloc_calls == 1);
	failures += check(directory->dd_fd == 7 && directory->dd_seek == 0 &&
	    directory->dd_loc == 0 && directory->dd_size == 0);
	failures += check(discobsd_telldir(directory) == 0 && lseek_calls == 0);

	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 11 &&
	    bytes_equal(entry->d_name, "a"));
	failures += check(discobsd_telldir(directory) == 12 && lseek_calls == 0);
	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 22 &&
	    bytes_equal(entry->d_name, "b"));
	failures += check(discobsd_telldir(directory) == DIRBLKSIZ);

	discobsd_seekdir(directory, 12);
	failures += check(lseek_calls == 0 && read_calls == 1);
	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 22);

	place_entry(0, 0, 44, 12, "z");
	discobsd_seekdir(directory, 0);
	failures += check(lseek_calls == 1 && directory->dd_size == 0);
	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 44 &&
	    bytes_equal(entry->d_name, "z") && read_calls == 2);
	discobsd_seekdir(directory, DIRBLKSIZ);
	failures += check(lseek_calls == 1);
	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 33);
	failures += check(read_calls == 3 && directory->dd_seek == DIRBLKSIZ);

	fail_lseek = 1;
	discobsd_seekdir(directory, 0);
	failures += check(lseek_calls == 2 && directory->dd_seek == DIRBLKSIZ &&
	    discobsd_telldir(directory) == 2 * DIRBLKSIZ);
	fail_lseek = 0;
	discobsd_seekdir(directory, 12);
	failures += check(lseek_calls == 3);
	entry = discobsd_readdir(directory);
	failures += check(entry != NULL && entry->d_ino == 22 && read_calls == 4);

	discobsd_closedir(directory);
	failures += check(close_calls == 1 && free_calls == 1);
	return failures;
}

static int
test_failures(void)
{
	DIR *directory;
	int failures = 0;

	reset_fixture();
	fail_open = 1;
	directory = discobsd_opendir("fixture");
	failures += check(directory == NULL && malloc_calls == 0 &&
	    close_calls == 0 && dirent_test_errno == ENOENT);

	reset_fixture();
	fail_malloc = 1;
	directory = discobsd_opendir("fixture");
	failures += check(directory == NULL && close_calls == 1 &&
	    dirent_test_errno == ENOMEM);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	fail_read = 1;
	failures += check(discobsd_readdir(directory) == NULL &&
	    directory->dd_size == 0 && directory->dd_loc == 0);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	read_length = 4;
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	place_entry(0, 0, 11, DIRBLKSIZ + 4, "a");
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	place_entry(0, 0, 11, 0, "a");
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	place_entry(0, 0, 11, 10, "a");
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	place_entry(0, 0, 11, 12, "a");
	((struct direct *)directory_blocks[0])->d_namlen = MAXNAMLEN + 1;
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);

	reset_fixture();
	directory = discobsd_opendir("fixture");
	((struct direct *)directory_blocks[0])->d_name[1] = 'x';
	dirent_test_errno = 0;
	failures += check(discobsd_readdir(directory) == NULL &&
	    dirent_test_errno == EIO);
	discobsd_closedir(directory);
	return failures;
}

static int
check_named(int condition, const char *label)
{
	size_t label_length = 0;

	if (condition)
		return 0;
	while (label[label_length] != '\0')
		++label_length;
	(void)write(STDERR_FILENO, "FAIL: ", 6);
	(void)write(STDERR_FILENO, label, label_length);
	(void)write(STDERR_FILENO, "\n", 1);
	return 1;
}

static int
test_admission(void)
{
	DIR *directory;
	int failures = 0;

	reset_fixture();
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == &allocated_directory &&
	    open_calls == 1 && fstat_calls == 1 && fcntl_calls == 1 &&
	    malloc_calls == 1 && close_calls == 0,
	    "directory admission call counts");
	failures += check_named(operation_count == 4 &&
	    operation_trace[0] == OPERATION_OPEN &&
	    operation_trace[1] == OPERATION_FSTAT &&
	    operation_trace[2] == OPERATION_FCNTL &&
	    operation_trace[3] == OPERATION_MALLOC,
	    "directory validation precedes allocation");
	failures += check_named(test_fcntl(7, F_GETFD) == FD_CLOEXEC,
	    "directory descriptor has close-on-exec");
	if (directory != NULL)
		discobsd_closedir(directory);
	failures += check_named(close_calls == 1 && free_calls == 1 &&
	    !descriptor_active, "directory success transfers one descriptor");

	reset_fixture();
	fail_open = 1;
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == NULL && fstat_calls == 0 &&
	    fcntl_calls == 0 && malloc_calls == 0 && close_calls == 0 &&
	    dirent_test_errno == ENOENT, "open failure owns no descriptor");

	reset_fixture();
	fail_fstat = 1;
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == NULL && fstat_calls == 1 &&
	    fcntl_calls == 0 && malloc_calls == 0,
	    "metadata failure stops directory admission");
	failures += check_named(close_calls == 1 && !descriptor_active &&
	    free_calls == 0, "metadata failure closes descriptor once");
	failures += check_named(dirent_test_errno == EIO,
	    "metadata cleanup preserves deciding errno");

	reset_fixture();
	opened_mode = S_IFREG | 0644;
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == NULL && fcntl_calls == 0 &&
	    malloc_calls == 0 && close_calls == 1 && free_calls == 0 &&
	    !descriptor_active && dirent_test_errno == ENOTDIR,
	    "regular file rejects directory admission");

	reset_fixture();
	fail_fcntl = 1;
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == NULL && fstat_calls == 1 &&
	    fcntl_calls == 1 && malloc_calls == 0 && close_calls == 1 &&
	    !descriptor_active && free_calls == 0 && dirent_test_errno == EBADF,
	    "close-on-exec failure stops directory admission");

	reset_fixture();
	fail_malloc = 1;
	directory = discobsd_opendir("fixture");
	failures += check_named(directory == NULL && fstat_calls == 1 &&
	    fcntl_calls == 1 && malloc_calls == 1 && close_calls == 1 &&
	    !descriptor_active && free_calls == 0 && dirent_test_errno == ENOMEM,
	    "allocation failure closes validated descriptor");
	return failures;
}

static int
test_close_ownership(void)
{
	static const int errors[] = { EBADF, EIO, EINTR };
	unsigned error_index;
	DIR *directory;
	int failures = 0;

	reset_fixture();
	directory = discobsd_opendir("fixture");
	free_error = ENOMEM;
	failures += check_named(discobsd_closedir(directory) == 0,
	    "closedir reports close success");
	failures += check_named(close_calls == 1 && free_calls == 1 &&
	    !descriptor_active, "closedir success releases ownership once");

	for (error_index = 0;
	    error_index < sizeof(errors) / sizeof(errors[0]); ++error_index) {
		reset_fixture();
		directory = discobsd_opendir("fixture");
		close_error = errors[error_index];
		free_error = ENOMEM;
		failures += check_named(discobsd_closedir(directory) == -1,
		    "closedir reports close failure");
		failures += check_named(dirent_test_errno == errors[error_index],
		    "closedir preserves close errno across free");
		failures += check_named(close_calls == 1 && free_calls == 1 &&
		    !descriptor_active, "closedir failure releases ownership once");
	}
	return failures;
}

int
main(void)
{
	int failures = 0;

	failures += test_close_ownership();
	failures += test_admission();
	failures += test_stream_positions();
	failures += test_failures();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
