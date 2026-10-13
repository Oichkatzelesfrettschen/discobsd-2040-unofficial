/*
 * Native Smaller C acceptance for the public stdio and directory layouts.
 * The fixture uses only constructs that the maintained compiler already
 * accepts; later public-header migrations add their own construct probes.
 */
#include <sys/types.h>
#include <sys/dir.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int
scan_variadic(const char *input, const char *format, ...)
{
	va_list arguments;
	int assignments;

	va_start(arguments, format);
	assignments = vsscanf(input, format, arguments);
	va_end(arguments);
	return assignments;
}

static int
check_layouts(void)
{
	if (sizeof(FILE) != 24 || offsetof(FILE, _cnt) != 0 ||
	    offsetof(FILE, _ptr) != 4 || offsetof(FILE, _base) != 8 ||
	    offsetof(FILE, _bufsiz) != 12 || offsetof(FILE, _flag) != 16 ||
	    offsetof(FILE, _file) != 18 || offsetof(FILE, _ub) != 20 ||
	    sizeof(((FILE *)0)->_ub) != 1)
		return 1;
	if (sizeof(DIR) != 1040 || offsetof(DIR, dd_fd) != 0 ||
	    offsetof(DIR, dd_seek) != 4 || offsetof(DIR, dd_loc) != 8 ||
	    offsetof(DIR, dd_size) != 12 || offsetof(DIR, dd_buf) != 16 ||
	    sizeof(((DIR *)0)->dd_buf) != 1024)
		return 2;
	if (sizeof(void *) != 4 || sizeof(int) != 4 || sizeof(long) != 4 ||
	    sizeof(size_t) != 4 || sizeof(fpos_t) != 4 || sizeof(short) != 2 ||
	    sizeof(char) != 1)
		return 3;
	return 0;
}

static int
check_stream_input(void)
{
	FILE peer_stream;
	char scanned_word[5];
	char line[8];
	int scanned_number;
	int input_byte;

	memset(&peer_stream, 0, sizeof(peer_stream));
	peer_stream._flag = _IOREAD | _IONBF;
	peer_stream._file = 0;
	setbuf(stdin, NULL);

	if (getc(stdin) != 'A')
		return 10;
	if (ungetc('Z', stdin) != 'Z')
		return 11;
	if (getc(&peer_stream) != 'B')
		return 12;
	if (stdin->_base != (char *)stdin->_ub ||
	    peer_stream._base != (char *)peer_stream._ub ||
	    stdin->_base == peer_stream._base)
		return 13;
	if (getc(stdin) != 'Z' || getc(stdin) != '\n')
		return 14;

	if (scanf("%d %4s", &scanned_number, scanned_word) != 2 ||
	    scanned_number != 42 || strcmp(scanned_word, "word") != 0)
		return 15;
	if (getc(stdin) != '\n')
		return 16;
	if (fgets(line, sizeof(line), stdin) == NULL ||
	    strcmp(line, "line\n") != 0)
		return 17;
	input_byte = getc(stdin);
	if (input_byte != EOF || !feof(stdin) || ferror(stdin))
		return 18;
	clearerr(stdin);
	if (feof(stdin) || ferror(stdin))
		return 19;
	return 0;
}

static int
check_string_scanning(void)
{
	char scanned_word[5];
	int decimal_value;
	int inferred_value;
	int consumed;

	consumed = -1;
	if (sscanf("17 0x2a word", "%d %i %4s%n", &decimal_value,
	    &inferred_value, scanned_word, &consumed) != 3 ||
	    decimal_value != 17 || inferred_value != 42 ||
	    strcmp(scanned_word, "word") != 0 || consumed != 12)
		return 20;
	decimal_value = 0;
	inferred_value = 0;
	if (scan_variadic("9 010", "%d %i", &decimal_value,
	    &inferred_value) != 2 || decimal_value != 9 || inferred_value != 8)
		return 21;
	return 0;
}

static int
check_string_stream(void)
{
	FILE string_stream;
	char string_bytes[3];

	string_bytes[0] = 'a';
	string_bytes[1] = 'b';
	string_bytes[2] = '\0';
	memset(&string_stream, 0, sizeof(string_stream));
	string_stream._cnt = 2;
	string_stream._ptr = string_bytes;
	string_stream._base = string_bytes;
	string_stream._bufsiz = 2;
	string_stream._flag = _IOREAD | _IOSTRG;
	string_stream._file = -1;

	if (getc(&string_stream) != 'a')
		return 22;
	if (ungetc('Z', &string_stream) != 'Z')
		return 22;
	if ((string_stream._flag & _IOUNGET) == 0)
		return 22;
	if (getc(&string_stream) != 'Z')
		return 22;
	if ((string_stream._flag & _IOUNGET) != 0)
		return 22;
	if (getc(&string_stream) != 'b')
		return 22;
	if (getc(&string_stream) != EOF)
		return 22;
	return 0;
}

static int
check_bounded_output(void)
{
	char complete[8];
	char truncated[4];

	memset(complete, 'X', sizeof(complete));
	if (snprintf(complete, sizeof(complete), "%s:%d", "x", 42) != 4 ||
	    memcmp(complete, "x:42\0", 5) != 0)
		return 23;
	memset(truncated, 'X', sizeof(truncated));
	if (snprintf(truncated, sizeof(truncated), "%s", "abcdef") != 6 ||
	    memcmp(truncated, "abc\0", 4) != 0)
		return 24;
	return 0;
}

static int
check_directory_close(void)
{
	DIR *directory;

	directory = malloc(sizeof(DIR));
	if (directory == NULL)
		return 25;
	directory->dd_fd = -1;
	errno = 0;
	if (closedir(directory) != -1 || errno != EBADF)
		return 26;
	return 0;
}

int
main(void)
{
	int status;

	status = check_layouts();
	if (status != 0)
		return status;
	puts("PASS native stdio layouts");
	status = check_stream_input();
	if (status != 0)
		return status;
	puts("PASS stream-owned input");
	status = check_string_scanning();
	if (status != 0)
		return status;
	puts("PASS scanf and vsscanf");
	status = check_string_stream();
	if (status != 0)
		return status;
	puts("PASS string pushback");
	status = check_bounded_output();
	if (status != 0)
		return status;
	puts("PASS bounded output");
	status = check_directory_close();
	if (status != 0)
		return status;
	puts("PASS directory close result");
	return 0;
}
