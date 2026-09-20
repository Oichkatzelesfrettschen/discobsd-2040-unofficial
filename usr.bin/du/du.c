#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

struct hard_link {
	struct hard_link *next;
	dev_t device;
	ino_t inode;
	unsigned long remaining;
};

#ifndef DU_PATH_SIZE
#define DU_PATH_SIZE BUFSIZ
#endif

static char path[DU_PATH_SIZE];
static int print_all;
static int summary_only;
static int traversal_error;
static int track_hard_links = 1;
static DIR *directory_stream;
static struct hard_link *hard_links;

#define kb(blocks) howmany((blocks) * DEV_BSIZE, 1024)

static long descend(const char *);

static void
free_hard_links(void)
{
	while (hard_links != NULL) {
		struct hard_link *record = hard_links;

		hard_links = record->next;
		free(record);
	}
}

static int
hard_link_seen(const struct stat *status)
{
	struct hard_link **record_slot;
	struct hard_link *record;

	if (status->st_nlink <= 1 || !track_hard_links)
		return 0;
	for (record_slot = &hard_links; *record_slot != NULL;
	    record_slot = &(*record_slot)->next) {
		record = *record_slot;
		if (record->device != status->st_dev ||
		    record->inode != status->st_ino)
			continue;
		if (--record->remaining == 0) {
			*record_slot = record->next;
			free(record);
		}
		return 1;
	}
	record = malloc(sizeof(*record));
	if (record == NULL) {
		fprintf(stderr,
		    "du: hard-link tracking exhausted; later links are counted\n");
		free_hard_links();
		track_hard_links = 0;
		traversal_error = 1;
		return 0;
	}
	record->device = status->st_dev;
	record->inode = status->st_ino;
	record->remaining = (unsigned long)status->st_nlink - 1;
	record->next = hard_links;
	hard_links = record;
	return 0;
}

static int
append_component(const char *component, char **saved_end,
    const char **nested_name)
{
	size_t component_length = strlen(component);
	char *end = path + strlen(path);
	size_t separator_length = end != path && end[-1] == '/' ? 0 : 1;

	if ((size_t)(end - path) + separator_length + component_length >=
	    sizeof(path)) {
		fprintf(stderr, "du: path too long under %s: %s\n", path,
		    component);
		traversal_error = 1;
		return -1;
	}
	*saved_end = end;
	if (separator_length != 0)
		*end++ = '/';
	*nested_name = end;
	memcpy(end, component, component_length + 1);
	return 0;
}

static long
descend(const char *name)
{
	struct stat status;
	const struct direct *entry;
	char *original_end = path + strlen(path);
	long blocks;

	if (lstat(name, &status) < 0) {
		perror(path);
		traversal_error = 1;
		return 0;
	}
	if ((status.st_mode & S_IFMT) != S_IFDIR && hard_link_seen(&status))
		return 0;
	blocks = status.st_blocks;
	if ((status.st_mode & S_IFMT) != S_IFDIR) {
		if (print_all)
			printf("%ld\t%s\n", kb(blocks), path);
		return blocks;
	}

	if (directory_stream != NULL)
		(void)closedir(directory_stream);
	directory_stream = opendir(name);
	if (directory_stream == NULL) {
		perror(path);
		traversal_error = 1;
		return 0;
	}
	if (chdir(name) < 0) {
		perror(path);
		traversal_error = 1;
		(void)closedir(directory_stream);
		directory_stream = NULL;
		return 0;
	}
	while ((entry = readdir(directory_stream)) != NULL) {
		long directory_offset;
		const char *nested_name;
		char *saved_end;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		if (append_component(entry->d_name, &saved_end,
		    &nested_name) < 0)
			continue;
		directory_offset = telldir(directory_stream);
		blocks += descend(nested_name);
		*saved_end = '\0';
		if (directory_stream == NULL) {
			directory_stream = opendir(".");
			if (directory_stream == NULL) {
				perror(path);
				traversal_error = 1;
				break;
			}
			seekdir(directory_stream, directory_offset);
		}
	}
	if (directory_stream != NULL)
		(void)closedir(directory_stream);
	directory_stream = NULL;
	if (!summary_only)
		printf("%ld\t%s\n", kb(blocks), path);
	if (chdir("..") < 0) {
		perror(path);
		traversal_error = 1;
	}
	*original_end = '\0';
	return blocks;
}

static int
process_operand(const char *operand)
{
	char *last_slash;
	const char *name;
	size_t length = strlen(operand);
	long blocks;

	if (length >= sizeof(path)) {
		fprintf(stderr, "du: path too long: %s\n", operand);
		return 1;
	}
	memcpy(path, operand, length + 1);
	last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
		name = path[0] != '\0' ? path : ".";
	} else {
		name = last_slash[1] != '\0' ? last_slash + 1 : ".";
		*last_slash = '\0';
		if (chdir(path[0] != '\0' ? path : "/") < 0) {
			perror(path[0] != '\0' ? path : "/");
			return 1;
		}
		*last_slash = '/';
	}
	blocks = descend(name);
	if (summary_only)
		printf("%ld\t%s\n", kb(blocks), path);
	free_hard_links();
	return traversal_error != 0;
}

int
main(int argc, char **argv)
{
	int argument = 1;
	int result = 0;
	char *default_operand = ".";

	while (argument < argc) {
		if (strcmp(argv[argument], "-a") == 0)
			print_all = 1;
		else if (strcmp(argv[argument], "-s") == 0)
			summary_only = 1;
		else
			break;
		argument++;
	}
	if (argument == argc) {
		argv = &default_operand;
		argc = 1;
		argument = 0;
	}
	for (; argument < argc; argument++) {
		if (argument + 1 < argc) {
			pid_t child = fork();
			int child_status;
			pid_t waited;

			if (child < 0) {
				perror("du: fork");
				return 1;
			}
			if (child == 0)
				return process_operand(argv[argument]);
			do {
				waited = waitpid(child, &child_status, 0);
			} while (waited < 0 && errno == EINTR);
			if (waited < 0) {
				perror("du: wait");
				result = 1;
			} else if (!WIFEXITED(child_status) ||
			    WEXITSTATUS(child_status) != 0) {
				result = 1;
			}
			continue;
		}
		if (process_operand(argv[argument]) != 0)
			result = 1;
	}
	return result;
}
