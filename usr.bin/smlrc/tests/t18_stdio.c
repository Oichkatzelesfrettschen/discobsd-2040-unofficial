/*
 * Native Smaller C acceptance for public stdio, directory and password
 * layouts. The fixture uses the maintained native language subset.
 */
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/glob.h>
#include <sys/glob.h>
#include <pwd.h>
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

struct passwd_alignment_probe {
	char alignment_byte;
	struct passwd entry;
};

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
check_passwd_layout(void)
{
	if (sizeof(struct passwd) != 28 ||
	    offsetof(struct passwd, pw_name) != 0 ||
	    offsetof(struct passwd, pw_passwd) != 4 ||
	    offsetof(struct passwd, pw_uid) != 8 ||
	    offsetof(struct passwd, pw_gid) != 12 ||
	    offsetof(struct passwd, pw_gecos) != 16 ||
	    offsetof(struct passwd, pw_dir) != 20 ||
	    offsetof(struct passwd, pw_shell) != 24)
		return 4;
	if (sizeof(((struct passwd *)0)->pw_uid) != 4 ||
	    sizeof(((struct passwd *)0)->pw_gid) != 4 ||
	    offsetof(struct passwd_alignment_probe, entry) != 4)
		return 5;
	if (_PW_KEYBYNAME != '0' || _PW_KEYBYUID != '1')
		return 6;
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
	errno = 0;
	if (snprintf(NULL, 0, "%2147483648d", 1) != EOF || errno != EOVERFLOW)
		return 29;
	if (snprintf(NULL, 0, "%2147483647s", "") != 2147483647)
		return 30;
	if (snprintf(complete, sizeof(complete), "%#.3o", 1) != 3 ||
	    strcmp(complete, "001") != 0)
		return 31;
	return 0;
}

static int
check_stream_output(void)
{
	FILE stream;
	char buffer[16];

	memset(&stream, 0, sizeof(stream));
	stream._flag = _IOWRT;
	stream._base = stream._ptr = buffer;
	stream._bufsiz = sizeof(buffer);
	stream._cnt = sizeof(buffer);
	if (fprintf(&stream, "%s:%d", "x", 42) != 4 ||
	    stream._ptr != buffer + 4 || memcmp(buffer, "x:42", 4) != 0)
		return 27;
	memset(&stream, 0, sizeof(stream));
	stream._flag = _IOWRT | _IONBF;
	stream._file = -1;
	if (fputs("abc", &stream) != EOF || !ferror(&stream) ||
	    stream._base != NULL || stream._ptr != NULL || stream._cnt != 0)
		return 28;
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
	status = check_passwd_layout();
	if (status != 0)
		return status;
	puts("PASS password header layout");
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
	if (printf("PASS bounded output\n") != 20)
		return 32;
	status = check_stream_output();
	if (status != 0)
		return status;
	puts("PASS stream output contracts");
	status = check_directory_close();
	if (status != 0)
		return status;
	puts("PASS directory close result");
	return 0;
}
