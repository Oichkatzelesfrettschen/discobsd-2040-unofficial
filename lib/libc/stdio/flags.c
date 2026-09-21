/*
 * Translate a stream mode string into the open(2) flags and the stream flag
 * that go with it, for the three entry points that take one: fopen, freopen
 * and fdopen. One reading of the string governs all of them, so a mode the
 * standard defines is accepted the same way wherever it arrives.
 *
 * C17 7.21.5.3p3 fixes the vocabulary: r, w and a choose the direction, a
 * '+' anywhere after the first character makes the stream an update stream,
 * 'b' selects binary and has no effect where text and binary streams are the
 * same bytes, and 'x' makes a w mode fail rather than truncate an existing
 * file. The scan reads the whole string rather than indexing fixed positions,
 * because '+' and 'x' are ordered freely against 'b': "rb+" and "r+b" are
 * both update modes and "wb+x" puts 'x' at the fourth character.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

int
_sflags(const char *mode, int *oflags)
{
	const char *cursor;
	int flags, update, exclusive;

	/*
	 * The scan starts at the first character rather than past it, so an
	 * empty mode string reads only its terminator; r, w and a are neither
	 * '+' nor 'x', and any other first character is refused below.
	 */
	update = 0;
	exclusive = 0;
	for (cursor = mode; *cursor != '\0'; cursor++) {
		if (*cursor == '+')
			update = 1;
		else if (*cursor == 'x')
			exclusive = 1;
	}

	switch (*mode) {
	case 'r':
		flags = update ? O_RDWR : O_RDONLY;
		break;
	case 'w':
		flags = O_TRUNC | O_CREAT | (update ? O_RDWR : O_WRONLY);
		/*
		 * C17 7.21.5.3p3 defines 'x' for the w modes alone, where it
		 * makes the create fail on a file that already exists rather
		 * than truncating it.
		 */
		if (exclusive)
			flags = (flags & ~O_TRUNC) | O_EXCL;
		break;
	case 'a':
		/*
		 * O_APPEND rather than one lseek at open time: C17 7.21.5.3p6
		 * forces every write to the end of the file, which survives a
		 * seek and another writer, and sys/kern/sys_inode.c sets
		 * uio_offset to i_size on each write to a regular file whose
		 * descriptor carries FAPPEND.
		 */
		flags = O_APPEND | O_CREAT | (update ? O_RDWR : O_WRONLY);
		break;
	default:
		errno = EINVAL;
		return (0);
	}

	*oflags = flags;
	if (update)
		return (_IORW);
	return (*mode == 'r' ? _IOREAD : _IOWRT);
}
