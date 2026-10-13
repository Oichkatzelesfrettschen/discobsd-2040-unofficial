#include <sys/types.h>
#include <sys/stat.h>
#define scandir discobsd_scandir
#define alphasort discobsd_alphasort
#include <sys/dir.h>
#undef alphasort
#undef scandir
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scandir_alloc_shim.h"

/* Resource probes admit headers without scanner declarations. */
#ifdef SCANDIR_RESOURCE_BASELINE
int discobsd_scandir(const char *, struct direct ***,
    int (*)(const struct direct *),
    int (*)(const struct direct **, const struct direct **));
int discobsd_alphasort(const struct direct **, const struct direct **);
#endif

#define MAX_SCRIPT_ENTRIES 20
#define MAX_OPEN_DIRECTORIES 4

struct scripted_entry {
	ino_t inode;
	unsigned short record_length;
	const char *name;
};

struct directory_script {
	const char *path;
	struct scripted_entry entries[MAX_SCRIPT_ENTRIES];
	unsigned entry_count;
	unsigned read_error_at;
	off_t metadata_size;
	mode_t mode;
	int fstat_error;
};

struct open_directory {
	DIR directory;
	struct directory_script *script;
	unsigned entry_index;
	int active;
};

static struct directory_script primary_script;
static struct directory_script nested_script;
static struct open_directory open_directories[MAX_OPEN_DIRECTORIES];
static struct direct reused_entry;
static unsigned close_calls;
static unsigned fstat_calls;
static unsigned open_calls;
static unsigned read_calls;
static int close_errno;
static int nested_scan_ran;
static int test_failures;

int dirent_test_errno;

static unsigned
text_length(const char *text)
{
	unsigned length = 0;

	while (text[length] != '\0')
		++length;
	return length;
}

static int
texts_equal(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		++left;
		++right;
	}
	return *left == *right;
}

static void
copy_text(char *destination, const char *source)
{
	while ((*destination++ = *source++) != '\0')
		;
}

static void
zero_bytes(void *buffer, unsigned length)
{
	unsigned char *bytes = buffer;

	while (length != 0) {
		*bytes++ = 0;
		--length;
	}
}

static void
report_failure(const char *label)
{
	static const char prefix[] = "FAIL: ";
	static const char newline[] = "\n";

	(void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
	(void)write(STDERR_FILENO, label, text_length(label));
	(void)write(STDERR_FILENO, newline, sizeof(newline) - 1);
	++test_failures;
}

static void
expect(int condition, const char *label)
{
	if (!condition)
		report_failure(label);
}

static void
reset_script(struct directory_script *script, const char *path)
{
	zero_bytes(script, sizeof(*script));
	script->path = path;
	script->read_error_at = UINT_MAX;
	script->mode = S_IFDIR | 0755;
}

static void
add_entry(struct directory_script *script, ino_t inode, const char *name,
    unsigned short record_length)
{
	struct scripted_entry *entry;

	expect(script->entry_count < MAX_SCRIPT_ENTRIES, "script entry capacity");
	if (script->entry_count >= MAX_SCRIPT_ENTRIES)
		return;
	entry = &script->entries[script->entry_count++];
	entry->inode = inode;
	entry->name = name;
	entry->record_length = record_length;
}

static void
reset_fixture(unsigned fail_at, int cleanup_errno_value)
{
	zero_bytes(open_directories, sizeof(open_directories));
	zero_bytes(&reused_entry, sizeof(reused_entry));
	reset_script(&primary_script, "fixture");
	reset_script(&nested_script, "nested");
	close_calls = 0;
	fstat_calls = 0;
	open_calls = 0;
	read_calls = 0;
	close_errno = cleanup_errno_value;
	nested_scan_ran = 0;
	dirent_test_errno = 0;
	scandir_alloc_reset(fail_at, ENOMEM, cleanup_errno_value);
}

static struct directory_script *
find_script(const char *path)
{
	if (texts_equal(path, primary_script.path))
		return &primary_script;
	if (texts_equal(path, nested_script.path))
		return &nested_script;
	return NULL;
}

static struct open_directory *
find_open_directory(DIR *directory)
{
	unsigned slot_index;

	for (slot_index = 0; slot_index < MAX_OPEN_DIRECTORIES; ++slot_index) {
		if (open_directories[slot_index].active &&
		    &open_directories[slot_index].directory == directory)
			return &open_directories[slot_index];
	}
	return NULL;
}

static struct open_directory *
find_descriptor(int descriptor)
{
	unsigned slot_index;

	for (slot_index = 0; slot_index < MAX_OPEN_DIRECTORIES; ++slot_index) {
		if (open_directories[slot_index].active &&
		    open_directories[slot_index].directory.dd_fd == descriptor)
			return &open_directories[slot_index];
	}
	return NULL;
}

DIR *
test_opendir(const char *path)
{
	struct directory_script *script;
	unsigned slot_index;

	++open_calls;
	script = find_script(path);
	if (script == NULL) {
		dirent_test_errno = ENOENT;
		return NULL;
	}
	for (slot_index = 0; slot_index < MAX_OPEN_DIRECTORIES; ++slot_index) {
		if (!open_directories[slot_index].active) {
			zero_bytes(&open_directories[slot_index],
			    sizeof(open_directories[slot_index]));
			open_directories[slot_index].active = 1;
			open_directories[slot_index].script = script;
			open_directories[slot_index].directory.dd_fd =
			    40 + (int)slot_index;
			return &open_directories[slot_index].directory;
		}
	}
	dirent_test_errno = EMFILE;
	return NULL;
}

int
test_fstat(int descriptor, struct stat *status)
{
	struct open_directory *opened;

	++fstat_calls;
	opened = find_descriptor(descriptor);
	if (opened == NULL || opened->script->fstat_error != 0) {
		dirent_test_errno = EIO;
		return -1;
	}
	zero_bytes(status, sizeof(*status));
	status->st_mode = opened->script->mode;
	status->st_size = opened->script->metadata_size;
	return 0;
}

struct direct *
test_readdir(DIR *directory)
{
	struct open_directory *opened;
	struct scripted_entry *scripted;
	unsigned name_length;

	++read_calls;
	opened = find_open_directory(directory);
	if (opened == NULL) {
		dirent_test_errno = EBADF;
		return NULL;
	}
	if (opened->entry_index == opened->script->read_error_at) {
		dirent_test_errno = EIO;
		return NULL;
	}
	if (opened->entry_index >= opened->script->entry_count)
		return NULL;
	scripted = &opened->script->entries[opened->entry_index++];
	zero_bytes(&reused_entry, sizeof(reused_entry));
	name_length = text_length(scripted->name);
	reused_entry.d_ino = scripted->inode;
	reused_entry.d_reclen = scripted->record_length;
	reused_entry.d_namlen = (unsigned short)name_length;
	copy_text(reused_entry.d_name, scripted->name);
	return &reused_entry;
}

int
test_closedir(DIR *directory)
{
	struct open_directory *opened;

	++close_calls;
	opened = find_open_directory(directory);
	if (opened != NULL)
		opened->active = 0;
	if (close_errno != 0)
		dirent_test_errno = close_errno;
	return close_errno == 0 ? 0 : -1;
}

static void
release_namelist(struct direct **names, int entry_count)
{
	while (entry_count > 0)
		test_free(names[--entry_count]);
	test_free(names);
}

static int
select_none(const struct direct *entry)
{
	(void)entry;
	return 0;
}

static int
select_odd_inode(const struct direct *entry)
{
	dirent_test_errno = EBUSY;
	return (entry->d_ino & 1) != 0;
}

static int
compare_names(const struct direct **left, const struct direct **right)
{
	return strcmp((*left)->d_name, (*right)->d_name);
}

static int
compare_names_reverse(const struct direct **left, const struct direct **right)
{
	return strcmp((*right)->d_name, (*left)->d_name);
}

static int
compare_names_nested(const struct direct **left, const struct direct **right)
{
	struct direct **nested_names = NULL;
	int nested_count;

	if (!nested_scan_ran) {
		nested_scan_ran = 1;
		nested_count = discobsd_scandir("nested", &nested_names, NULL,
		    compare_names_reverse);
		expect(nested_count == 3, "nested comparator scan count");
		if (nested_count == 3) {
			expect(texts_equal(nested_names[0]->d_name, "z"),
			    "nested comparator reverse order");
			release_namelist(nested_names, nested_count);
		}
	}
	return strcmp((*left)->d_name, (*right)->d_name);
}

static void
populate_primary(void)
{
	primary_script.metadata_size = DIRBLKSIZ;
	add_entry(&primary_script, 9, "delta", DIRBLKSIZ);
	add_entry(&primary_script, 2, "alpha", DIRBLKSIZ);
	add_entry(&primary_script, 7, "charlie", DIRBLKSIZ);
	add_entry(&primary_script, 4, "bravo", DIRBLKSIZ);
	add_entry(&primary_script, 5, "echo", DIRBLKSIZ);
	add_entry(&primary_script, 6, "foxtrot", DIRBLKSIZ);
	add_entry(&primary_script, 3, "golf", DIRBLKSIZ);
	add_entry(&primary_script, 8, "hotel", DIRBLKSIZ);
	add_entry(&primary_script, 1, "india", DIRBLKSIZ);
}

static void
populate_nested(void)
{
	add_entry(&nested_script, 1, "x", DIRBLKSIZ);
	add_entry(&nested_script, 2, "z", DIRBLKSIZ);
	add_entry(&nested_script, 3, "y", DIRBLKSIZ);
}

static void
test_empty_and_rejected(void)
{
	struct direct **names;
	int entry_count;

	reset_fixture(0, 0);
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == 0 && names == NULL,
	    "empty scan returns safely freeable output");
	expect(scandir_alloc_calls() == 0 && close_calls == 1 && fstat_calls == 1,
	    "empty scan owns no vector allocation");

	reset_fixture(0, 0);
	populate_primary();
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, select_none, NULL);
	expect(entry_count == 0 && names == NULL,
	    "all-rejected scan returns safely freeable output");
	expect(scandir_alloc_calls() == 0 && scandir_alloc_live_count() == 0,
	    "rejected entries consume no allocations");
}

static void
test_growth_and_copies(void)
{
	static const char *expected_names[] = {
		"delta", "alpha", "charlie", "bravo", "echo",
		"foxtrot", "golf", "hotel", "india"
	};
	struct direct **names = NULL;
	unsigned entry_index;
	unsigned expected_live_bytes;
	unsigned expected_peak_bytes;
	unsigned record_size;
	int entry_count;

	reset_fixture(0, 0);
	populate_primary();
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == 9, "selected-entry growth count");
	expect(fstat_calls == 1, "metadata read occurs once");
	expect(scandir_alloc_request_count() == 12,
	    "selected entries drive vector growth");
	expect(scandir_alloc_request_size(0) == 4 * sizeof(*names) &&
	    !scandir_alloc_request_is_realloc(0),
	    "geometric vector capacity");
	expect(scandir_alloc_request_size(5) == 8 * sizeof(*names) &&
	    scandir_alloc_request_is_realloc(5),
	    "first geometric realloc boundary");
	expect(scandir_alloc_request_size(10) == 16 * sizeof(*names) &&
	    scandir_alloc_request_is_realloc(10),
	    "second geometric realloc boundary");
	if (entry_count == 9) {
		expected_live_bytes = 16 * sizeof(*names);
		for (entry_index = 0; entry_index < 9; ++entry_index) {
			expect(texts_equal(names[entry_index]->d_name,
			    expected_names[entry_index]),
			    "reused readdir storage yields independent copies");
			record_size = DIRSIZ(names[entry_index]);
			expect(scandir_allocation_size(names[entry_index]) == record_size,
			    "selected record uses compact DIRSIZ allocation");
			expect(names[entry_index]->d_reclen == record_size,
			    "compact copy records allocated length");
			expected_live_bytes +=
			    scandir_allocation_size(names[entry_index]);
		}
		expect(scandir_alloc_live_bytes() == expected_live_bytes,
		    "retained requested bytes match compact ownership");
		expected_peak_bytes = 24 * sizeof(*names);
		for (entry_index = 0; entry_index < 8; ++entry_index)
			expected_peak_bytes +=
			    scandir_allocation_size(names[entry_index]);
		expect(scandir_alloc_peak_bytes() == expected_peak_bytes,
		    "moving realloc peak remains bounded");
		release_namelist(names, entry_count);
	}
	expect(scandir_alloc_live_count() == 0 &&
	    scandir_alloc_invalid_frees() == 0,
	    "successful scan transfers exact allocation ownership");
}

static void
test_selection_and_sorting(void)
{
	static const char maximum_name[] =
	    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
	static const char *sorted_names[] = {
		"alpha", "bravo", "charlie", "delta", "echo",
		"foxtrot", "golf", "hotel", "india"
	};
	struct direct **names = NULL;
	const struct direct *left;
	const struct direct *right;
	unsigned entry_index;
	int entry_count;
	int comparison;

	_Static_assert(sizeof(maximum_name) == MAXNAMLEN + 1,
	    "maximum-name fixture must fill d_name");

	reset_fixture(0, 0);
	populate_primary();
	entry_count = discobsd_scandir("fixture", &names, select_odd_inode,
	    compare_names);
	expect(entry_count == 5, "selector result count");
	expect(scandir_alloc_calls() == 7,
	    "selector allocates records only after acceptance");
	expect(entry_count >= 0, "selector errno does not become read failure");
	if (entry_count == 5)
		release_namelist(names, entry_count);

	reset_fixture(0, 0);
	populate_primary();
	entry_count = discobsd_scandir("fixture", &names, NULL, compare_names);
	expect(entry_count == 9, "typed comparator count");
	if (entry_count == 9) {
		for (entry_index = 0; entry_index < 9; ++entry_index)
			expect(texts_equal(names[entry_index]->d_name,
			    sorted_names[entry_index]), "typed comparator order");
		release_namelist(names, entry_count);
	}

	reset_fixture(0, 0);
	populate_primary();
	populate_nested();
	entry_count = discobsd_scandir("fixture", &names, NULL,
	    compare_names_nested);
	expect(entry_count == 9 && nested_scan_ran,
	    "nested comparator scan executes");
	if (entry_count == 9) {
		for (entry_index = 0; entry_index < 9; ++entry_index)
			expect(texts_equal(names[entry_index]->d_name,
			    sorted_names[entry_index]),
			    "nested comparator retains outer order");
		release_namelist(names, entry_count);
	}

	reset_fixture(0, 0);
	add_entry(&primary_script, 1, "a", DIRBLKSIZ);
	add_entry(&primary_script, 2, "b", DIRBLKSIZ);
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == 2, "alphasort fixture count");
	if (entry_count == 2) {
		left = names[0];
		right = names[1];
		dirent_test_errno = EACCES;
		comparison = discobsd_alphasort(&left, &right);
		expect(comparison < 0 && dirent_test_errno == EACCES,
		    "alphasort preserves errno");
		release_namelist(names, entry_count);
	}

	reset_fixture(0, 0);
	add_entry(&primary_script, 4, "same", DIRBLKSIZ);
	add_entry(&primary_script, 2, "alpha", DIRBLKSIZ);
	add_entry(&primary_script, 3, "same", DIRBLKSIZ);
	add_entry(&primary_script, 1, "zulu", DIRBLKSIZ);
	entry_count = discobsd_scandir("fixture", &names, NULL, compare_names);
	expect(entry_count == 4, "duplicate-key sort count");
	if (entry_count == 4) {
		expect(texts_equal(names[0]->d_name, "alpha") &&
		    texts_equal(names[1]->d_name, "same") &&
		    texts_equal(names[2]->d_name, "same") &&
		    texts_equal(names[3]->d_name, "zulu"),
		    "duplicate-key sort order");
		expect((names[1]->d_ino == 3 && names[2]->d_ino == 4) ||
		    (names[1]->d_ino == 4 && names[2]->d_ino == 3),
		    "duplicate-key sort preserves both records");
		release_namelist(names, entry_count);
	}

	reset_fixture(0, 0);
	add_entry(&primary_script, 1, maximum_name, DIRBLKSIZ);
	entry_count = discobsd_scandir("fixture", &names, NULL, compare_names);
	expect(entry_count == 1, "singleton maximum-name count");
	if (entry_count == 1) {
		expect(texts_equal(names[0]->d_name, maximum_name),
		    "maximum-length name copy");
		expect(names[0]->d_reclen == sizeof(*names[0]),
		    "maximum-length compact record size");
		release_namelist(names, entry_count);
	}
}

static void
test_interface_failures(void)
{
	struct direct **names;
	int entry_count;

	reset_fixture(0, EBUSY);
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("missing", &names, NULL, NULL);
	expect(entry_count == -1 && names == (struct direct **)(unsigned long)1 &&
	    dirent_test_errno == ENOENT && close_calls == 0,
	    "open failure leaves output sentinel unchanged");

	reset_fixture(0, EBUSY);
	primary_script.fstat_error = 1;
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == -1 && names == (struct direct **)(unsigned long)1 &&
	    close_calls == 1 && dirent_test_errno == EIO,
	    "fstat failure closes and preserves errno");

	reset_fixture(0, EBUSY);
	primary_script.mode = S_IFREG | 0644;
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == -1 && names == (struct direct **)(unsigned long)1 &&
	    close_calls == 1 && dirent_test_errno == ENOTDIR,
	    "non-directory metadata rejects input");

	reset_fixture(0, EBUSY);
	populate_primary();
	primary_script.read_error_at = 0;
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == -1 && names == (struct direct **)(unsigned long)1 &&
	    scandir_alloc_live_count() == 0 && dirent_test_errno == EIO,
	    "read failure before selection preserves errno");

	reset_fixture(0, EBUSY);
	populate_primary();
	primary_script.read_error_at = 3;
	names = (struct direct **)(unsigned long)1;
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == -1 && names == (struct direct **)(unsigned long)1,
	    "read failure after accepted records");
	expect(scandir_alloc_live_count() == 0 && dirent_test_errno == EIO,
	    "read failure cleanup preserves errno");
}

static void
test_allocation_failures(void)
{
	struct direct **names = NULL;
	unsigned allocation_ordinal;
	unsigned successful_calls;
	int entry_count;

	reset_fixture(0, 0);
	populate_primary();
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == 9, "allocation calibration success");
	successful_calls = scandir_alloc_calls();
	if (entry_count == 9)
		release_namelist(names, entry_count);

	for (allocation_ordinal = 1; allocation_ordinal <= successful_calls;
	    ++allocation_ordinal) {
		reset_fixture(allocation_ordinal, EBUSY);
		populate_primary();
		names = (struct direct **)(unsigned long)1;
		entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
		expect(entry_count == -1 &&
		    names == (struct direct **)(unsigned long)1,
		    "allocation failure leaves output sentinel unchanged");
		expect(scandir_alloc_live_count() == 0 &&
		    scandir_alloc_invalid_frees() == 0,
		    "allocation failure releases records");
		expect(dirent_test_errno == ENOMEM,
		    "allocation failure preserves errno");
		expect(close_calls == 1, "allocation failure closes once");
	}
}

static void
write_number(unsigned number)
{
	char digits[16];
	unsigned digit_count = 0;

	do {
		digits[digit_count++] = (char)('0' + number % 10);
		number /= 10;
	} while (number != 0);
	while (digit_count != 0)
		(void)write(STDOUT_FILENO, &digits[--digit_count], 1);
}

static int
report_resource_probe(void)
{
	struct direct **names = NULL;
	int entry_count;

	reset_fixture(0, 0);
	populate_primary();
	entry_count = discobsd_scandir("fixture", &names, NULL, NULL);
	expect(entry_count == 9, "resource probe count");
	write_number(scandir_alloc_live_bytes());
	(void)write(STDOUT_FILENO, " ", 1);
	write_number(scandir_alloc_peak_bytes());
	(void)write(STDOUT_FILENO, "\n", 1);
	if (entry_count == 9)
		release_namelist(names, entry_count);
	expect(scandir_alloc_live_count() == 0 &&
	    scandir_alloc_invalid_frees() == 0,
	    "resource probe releases ownership");
	return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main(int argument_count, char **arguments)
{
	int (*scan_function)(const char *, struct direct ***,
	    int (*)(const struct direct *),
	    int (*)(const struct direct **, const struct direct **));
	int (*sort_function)(const struct direct **, const struct direct **);

	if (argument_count == 2 &&
	    texts_equal(arguments[1], "--resource-probe"))
		return report_resource_probe();
	expect(argument_count == 1, "contract runner arguments");
	scan_function = discobsd_scandir;
	sort_function = discobsd_alphasort;
	expect(scan_function != NULL && sort_function != NULL,
	    "public callback types compile exactly");
	test_empty_and_rejected();
	test_growth_and_copies();
	test_selection_and_sorting();
	test_interface_failures();
	test_allocation_failures();
	return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
