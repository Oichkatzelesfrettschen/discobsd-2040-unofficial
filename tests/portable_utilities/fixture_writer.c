#include <sys/exec_aout.h>
#include <sys/exec_hsaout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>

static void
write_bytes(FILE *stream, const void *data, size_t length)
{
	if (fwrite(data, 1, length, stream) != length) {
		perror("fixture write");
		exit(1);
	}
}

static FILE *
open_fixture(const char *pathname)
{
	FILE *stream;

	stream = fopen(pathname, "wb");
	if (stream == NULL) {
		perror(pathname);
		exit(1);
	}
	return stream;
}

static void
write_aout_fixtures(void)
{
	static const unsigned char text[] = { 'T', 'E', 'X', 'T', 0 };
	static const unsigned char data[] = { 'D', 'A', 'T', 'A', 0 };
	static const unsigned char hidden[] = "HIDDEN";
	static const unsigned char packed_bytes[] = "PACKEDBYTES";
	struct exec header;
	FILE *stream;

	memset(&header, 0, sizeof(header));
	N_SETMAGIC(header, OMAGIC, MID_ZERO, 0);
	header.a_text = sizeof(text);
	header.a_data = sizeof(data);
	stream = open_fixture("raw.aout");
	write_bytes(stream, &header, sizeof(header));
	write_bytes(stream, text, sizeof(text));
	write_bytes(stream, data, sizeof(data));
	write_bytes(stream, hidden, sizeof(hidden));
	if (fclose(stream) != 0)
		exit(1);

	memset(&header, 0, sizeof(header));
	N_SETMAGIC(header, OMAGIC, MID_ZERO, EX_HSPACK);
	header.a_text = 128;
	stream = open_fixture("packed.aout");
	write_bytes(stream, &header, sizeof(header));
	write_bytes(stream, packed_bytes, sizeof(packed_bytes));
	if (fclose(stream) != 0)
		exit(1);
}

static void
set_user(struct utmp *record, const char *name)
{
	memset(record, 0, sizeof(*record));
	(void)strncpy(record->ut_name, name, sizeof(record->ut_name));
}

static void
write_user_fixtures(void)
{
	struct utmp record;
	FILE *stream;
	char name[sizeof(record.ut_name)];
	int index;

	stream = open_fixture("users.utmp");
	set_user(&record, "bob");
	write_bytes(stream, &record, sizeof(record));
	set_user(&record, "alice");
	write_bytes(stream, &record, sizeof(record));
	set_user(&record, "bob");
	write_bytes(stream, &record, sizeof(record));
	memset(&record, 0, sizeof(record));
	write_bytes(stream, &record, sizeof(record));
	if (fclose(stream) != 0)
		exit(1);

	stream = open_fixture("users-overflow.utmp");
	for (index = 0; index < 26; ++index) {
		(void)snprintf(name, sizeof(name), "u%02d", index);
		set_user(&record, name);
		write_bytes(stream, &record, sizeof(record));
	}
	if (fclose(stream) != 0)
		exit(1);
}

int
main(void)
{
	write_aout_fixtures();
	write_user_fixtures();
	return 0;
}
