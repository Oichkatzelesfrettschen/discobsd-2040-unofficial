/*
 * Host file access for v7_format_test.c, which compiles against the
 * kernel's headers and cannot include the C library's.
 */
#include <stdio.h>
#include <stdlib.h>

int
hk_host_load(const char *path, unsigned char **data, unsigned long *size)
{
	FILE *f = fopen(path, "rb");
	long n;

	if (f == NULL)
		return (-1);
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return (-1);
	}
	rewind(f);
	*data = malloc(n > 0 ? (size_t)n : 1);
	if (*data == NULL || fread(*data, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		return (-1);
	}
	*size = (unsigned long)n;
	return (fclose(f) == 0 ? 0 : -1);
}

int
hk_host_store(const char *path, const unsigned char *data, unsigned long size)
{
	FILE *f = fopen(path, "wb");

	if (f == NULL)
		return (-1);
	if (fwrite(data, 1, size, f) != size) {
		fclose(f);
		return (-1);
	}
	return (fclose(f) == 0 ? 0 : -1);
}

void
hk_host_exit(int status)
{
	exit(status);
}
