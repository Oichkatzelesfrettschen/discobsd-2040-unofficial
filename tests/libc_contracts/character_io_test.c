/* The target byte backend runs over bounded read/write dependencies. */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

struct _iobuf _iob[3];
int character_errno;
static unsigned char input_bytes[256];
static unsigned char output_bytes[512];
static size_t input_position, output_position;
static int io_failure, short_write, wrong_descriptor, allocations;
static int read_calls, write_calls;

_Static_assert(_Generic(&(getc), int (*)(FILE *): 1, default: 0), "getc signature");
_Static_assert(_Generic(&(putc), int (*)(int, FILE *): 1, default: 0), "putc signature");
_Static_assert(_Generic(&(getchar), int (*)(void): 1, default: 0), "getchar signature");
_Static_assert(_Generic(&(putchar), int (*)(int): 1, default: 0), "putchar signature");

int character_read(int, void *, size_t);
int character_write(int, const void *, size_t);
int character_fstat(int, struct stat *);
int character_isatty(int);
void *character_malloc(size_t);
void character_free(void *);
int character_close(int);
int _fwalk(int (*)(FILE *));

int
character_read(int descriptor, void *buffer, size_t length)
{

	++read_calls;
	wrong_descriptor |= descriptor != 5;
	if (io_failure) {
		character_errno = EIO;
		return -1;
	}
	if (length > sizeof(input_bytes) - input_position)
		length = sizeof(input_bytes) - input_position;
	memcpy(buffer, input_bytes + input_position, length);
	input_position += length;
	return (int)length;
}

int
character_write(int descriptor, const void *buffer, size_t length)
{

	++write_calls;
	wrong_descriptor |= descriptor != 6;
	if (io_failure) {
		character_errno = EIO;
		return -1;
	}
	if (short_write && length != 0)
		--length;
	if (length > sizeof(output_bytes) - output_position)
		return -1;
	memcpy(output_bytes + output_position, buffer, length);
	output_position += length;
	return (int)length;
}

int
character_fstat(int descriptor, struct stat *status)
{

	(void)descriptor;
	status->st_blksize = 16;
	return 0;
}

int
character_isatty(int descriptor)
{

	(void)descriptor;
	return 0;
}

void *
character_malloc(size_t length)
{

	(void)length;
	++allocations;
	return NULL;
}

void
character_free(void *pointer)
{

	(void)pointer;
}

int
character_close(int descriptor)
{

	(void)descriptor;
	return 0;
}

int
_fwalk(int (*function)(FILE *))
{

	(void)function;
	return 0;
}

static int (*volatile input_function)(FILE *);
static int (*volatile output_function)(int, FILE *);
static int (*volatile standard_input)(void) = (getchar);
static int (*volatile standard_output)(int) = (putchar);

static void
reset(FILE *stream, char *buffer, int flags, int descriptor)
{

	memset(stream, 0, sizeof(*stream));
	stream->_base = stream->_ptr = buffer;
	stream->_bufsiz = 16;
	stream->_flag = (short)flags;
	stream->_file = (short)descriptor;
	input_position = output_position = 0;
	io_failure = short_write = wrong_descriptor = allocations = 0;
	read_calls = write_calls = character_errno = 0;
}

static int
input_matrix(int entry, int unbuffered)
{
	FILE local, *stream = entry == 2 ? stdin : &local;
	char buffer[16];
	int value, result, calls;

	input_function = entry == 0 ? fgetc : (getc);
	reset(stream, buffer, _IOREAD | (unbuffered ? _IONBF : 0), 5);
	for (value = 0; value < 256; ++value) {
		result = entry == 2 ? standard_input() : input_function(stream);
		if (result != value || ferror(stream))
			return 11;
	}
	result = entry == 2 ? standard_input() : input_function(stream);
	if (result != EOF || !feof(stream) || ferror(stream))
		return 12;
	calls = read_calls;
	result = entry == 2 ? standard_input() : input_function(stream);
	if (result != EOF || stream->_cnt != 0 || read_calls != calls ||
	    wrong_descriptor || allocations)
		return 13;
	reset(stream, buffer, _IOREAD | (unbuffered ? _IONBF : 0), 5);
	io_failure = 1;
	result = entry == 2 ? standard_input() : input_function(stream);
	if (result != EOF || !ferror(stream) || feof(stream) || character_errno != EIO)
		return 14;
	clearerr(stream);
	io_failure = 0;
	result = entry == 2 ? standard_input() : input_function(stream);
	if (result != 0 || ferror(stream) || feof(stream))
		return 15;
	return 0;
}

static int
output_matrix(int entry, int mode)
{
	FILE local, *stream = entry == 2 ? stdout : &local;
	char buffer[16];
	int value, result, index;
	static const int edges[] = { INT_MIN, -1025, -257, -256, -1, 256, 257, 1024, INT_MAX };

	output_function = entry == 0 ? fputc : (putc);
	reset(stream, buffer, _IOWRT | (mode == 1 ? _IONBF : mode == 2 ? _IOLBF : 0), 6);
	for (value = 0; value < 256; ++value) {
		result = entry == 2 ? standard_output(value) : output_function(value, stream);
		if (result != value || ferror(stream))
			return 21;
	}
	if (fflush(stream) != 0 || output_position != 256 || allocations || wrong_descriptor)
		return 22;
	for (index = 0; index < 256; ++index)
		if (output_bytes[index] != (unsigned char)index)
			return 23;
	for (index = 0; index < (int)(sizeof(edges) / sizeof(edges[0])); ++index) {
		value = edges[index];
		result = entry == 2 ? standard_output(value) : output_function(value, stream);
		if (result != (unsigned char)value)
			return 24;
	}
	if (fflush(stream) != 0 || output_position != 256 + sizeof(edges) / sizeof(edges[0]))
		return 25;
	for (index = 0; index < (int)(sizeof(edges) / sizeof(edges[0])); ++index)
		if (output_bytes[256 + index] != (unsigned char)edges[index])
			return 26;
	reset(stream, buffer, _IOWRT | _IONBF, 6);
	io_failure = 1;
	result = entry == 2 ? standard_output('x') : output_function('x', stream);
	if (result != EOF || !ferror(stream) || character_errno != EIO)
		return 27;
	reset(stream, buffer, _IOWRT | _IONBF, 6);
	short_write = 1;
	result = entry == 2 ? standard_output('x') : output_function('x', stream);
	if (result != EOF || !ferror(stream))
		return 28;
	return 0;
}

int
main(void)
{
	int entry, mode, result;

	for (entry = 0; entry < 256; ++entry)
		input_bytes[entry] = (unsigned char)entry;
	for (entry = 0; entry < 3; ++entry) {
		for (mode = 0; mode < 2; ++mode) {
			result = input_matrix(entry, mode);
			if (result != 0)
				return result;
		}
		for (mode = 0; mode < 3; ++mode) {
			result = output_matrix(entry, mode);
			if (result != 0)
				return result;
		}
	}
	return 0;
}
