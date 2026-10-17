#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

long syscall(long, ...);
ssize_t sh_host_getdents(int, void *, size_t);

struct linux_dirent64 {
	uint64_t d_ino;
	int64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1];
};

int
main(int argc, char **argv)
{
	char adapted[4096];
	char raw[4096];
	ssize_t adapted_bytes;
	ssize_t raw_bytes;
	size_t adapted_offset;
	size_t raw_offset;
	int adapted_directory;
	int raw_directory;

	if (argc != 2) {
		fputs("usage: hostshim_layout_test directory\n", stderr);
		return 64;
	}
	if (sizeof(ino_t) < sizeof(uint64_t)) {
		fputs("host shell directory layout truncates ino_t\n", stderr);
		return 1;
	}
	if (sizeof(((struct dirent *)0)->d_ino) < sizeof(uint64_t)) {
		fputs("host shell directory layout truncates d_ino\n", stderr);
		return 2;
	}
	if (sizeof(((struct dirent *)0)->d_off) < sizeof(int64_t)) {
		fputs("host shell directory layout truncates d_off\n", stderr);
		return 3;
	}
	if (offsetof(struct dirent, d_ino) != 0 ||
	    offsetof(struct dirent, d_off) != 8 ||
	    offsetof(struct dirent, d_reclen) != 16 ||
	    offsetof(struct dirent, d_type) != 18 ||
	    offsetof(struct dirent, d_name) != 19) {
		fputs("host shell directory layout differs from getdents64\n",
		    stderr);
		return 4;
	}
	raw_directory = open(argv[1], O_RDONLY | O_DIRECTORY);
	adapted_directory = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (raw_directory < 0 || adapted_directory < 0) {
		fputs("host shell directory witness cannot open fixture\n", stderr);
		return 5;
	}
	raw_bytes = syscall(SYS_getdents64, raw_directory, raw, sizeof(raw));
	adapted_bytes = sh_host_getdents(adapted_directory, adapted,
	    sizeof(adapted));
	if (raw_bytes <= 0 || adapted_bytes <= 0) {
		fputs("host shell directory witness cannot read fixture\n", stderr);
		return 6;
	}
	adapted_offset = 0;
	raw_offset = 0;
	while (adapted_offset < (size_t)adapted_bytes &&
	    raw_offset < (size_t)raw_bytes) {
		struct linux_dirent64 *adapted_entry;
		struct linux_dirent64 *raw_entry;

		adapted_entry = (struct linux_dirent64 *)(adapted + adapted_offset);
		raw_entry = (struct linux_dirent64 *)(raw + raw_offset);
		if (adapted_entry->d_ino != raw_entry->d_ino ||
		    adapted_entry->d_off != raw_entry->d_off ||
		    adapted_entry->d_reclen != raw_entry->d_reclen ||
		    adapted_entry->d_type != raw_entry->d_type ||
		    strcmp(adapted_entry->d_name, raw_entry->d_name) != 0)
			break;
		adapted_offset += adapted_entry->d_reclen;
		raw_offset += raw_entry->d_reclen;
	}
	if (adapted_offset != (size_t)adapted_bytes ||
	    raw_offset != (size_t)raw_bytes) {
		fputs("host shell directory adapter rewrites kernel records\n",
		    stderr);
		return 7;
	}
	return 0;
}
