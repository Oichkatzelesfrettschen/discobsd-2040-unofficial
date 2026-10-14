/*
 * Map a uid to its login name by reading a passwd file with descriptor I/O.
 *
 * bin/sh owns the process break through alloc() and its stack arena, while
 * getpwuid(3) pulls the libc stdio allocator into the same address space and
 * establishes a second heap over it.  This parser keeps the shell free of
 * that allocator: it streams the file through one 64-byte stack buffer and
 * retains only the name and numeric uid fields of the record being read.
 *
 * A record matches when its uid field holds at least one digit, only
 * digits, and a value within unsigned int, so an empty field never stands
 * for uid 0.  A name longer than size - 1 bytes is truncated.  A missing
 * file, a read error or an absent uid stores "?", and no partial name from a
 * non-matching record survives in name.  The result is always name.
 */
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "pwname.h"

char *
pwname(const char *path, unsigned int userid, char *name, int size)
{
	char input_buffer[64];
	unsigned int parsed_uid = 0;
	unsigned int digit;
	int descriptor;
	int field = 0;
	int uid_valid = 1;
	int uid_digits = 0;
	int name_length = 0;
	int input_length;
	int input_offset;
	char character;

	descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
		goto unknown;
	while ((input_length = read(descriptor, input_buffer,
	    sizeof(input_buffer))) > 0) {
		for (input_offset = 0; input_offset < input_length; input_offset++) {
			character = input_buffer[input_offset];
			if (character == '\n') {
				if (field >= 3 && uid_valid && uid_digits > 0 &&
				    parsed_uid == userid && name_length > 0) {
					name[name_length] = 0;
					close(descriptor);
					return (name);
				}
				field = 0;
				uid_valid = 1;
				uid_digits = 0;
				name_length = 0;
				parsed_uid = 0;
				continue;
			}
			if (character == ':') {
				field++;
				continue;
			}
			if (field == 0) {
				if (name_length < size - 1)
					name[name_length++] = character;
				continue;
			}
			if (field != 2)
				continue;
			if (character < '0' || character > '9') {
				uid_valid = 0;
				continue;
			}
			digit = character - '0';
			uid_digits++;
			if (parsed_uid > (UINT_MAX - digit) / 10)
				uid_valid = 0;
			else if (uid_valid)
				parsed_uid = parsed_uid * 10 + digit;
		}
	}
	close(descriptor);
unknown:
	name[0] = '?';
	name[1] = 0;
	return (name);
}
