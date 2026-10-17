#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

int
main(void)
{
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
	return 0;
}
