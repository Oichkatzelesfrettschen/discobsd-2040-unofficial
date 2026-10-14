/*
 * Host test for bin/sh/pwname.c, the allocation-free passwd lookup behind
 * the prompt's \u.  Each case writes a passwd file into a private temporary
 * directory and calls the shipped pwname() with a name buffer prefilled with
 * 'x', so a result that leaks a partial name from a non-matching record shows
 * as a mismatch.  Records longer than the 64-byte read buffer exercise the
 * parser across read boundaries.
 *
 * Build and run: `make pwname && ./pwname_test-host`, or `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../pwname.h"

static int failures;
static char directory[] = "/tmp/pwname_test.XXXXXX";
static char path[sizeof(directory) + 16];

static void
write_passwd(const char *text)
{
	FILE *file;

	file = fopen(path, "w");
	if (file == NULL || fputs(text, file) == EOF || fclose(file) != 0) {
		perror(path);
		exit(2);
	}
}

static void
expect(const char *label, const char *lookup_path, unsigned int userid,
    int size, const char *want)
{
	char name[32];
	char *got;

	memset(name, 'x', sizeof(name));
	got = pwname(lookup_path, userid, name, size);
	if (got != name || strcmp(name, want) != 0) {
		printf("FAIL %s: uid %u gave \"%.*s\", want \"%s\"\n", label,
		    userid, (int)sizeof(name), name, want);
		failures++;
	} else
		printf("ok   %s\n", label);
}

int
main(void)
{
	if (mkdtemp(directory) == NULL) {
		perror("mkdtemp");
		return 2;
	}
	snprintf(path, sizeof(path), "%s/passwd", directory);

	write_passwd("root:*:0:0:Charlie &:/root:/bin/sh\n"
	    "operator::5:5:System &:/home/operator:/bin/sh\n"
	    "averyveryverylongaccountnamethatspansthereadbuffer:*:9:9:"
	    "a gecos field long enough to cross the next 64-byte read:/:/bin/sh\n"
	    "daemon:*:1:1:The devil himself:/root:/bin/sh\n");
	expect("first record", path, 0, 32, "root");
	expect("later record", path, 5, 32, "operator");
	expect("record after a cross-buffer record", path, 1, 32, "daemon");
	expect("long name truncates to the buffer", path, 9, 32,
	    "averyveryverylongaccountnametha");
	expect("absent uid stores ?", path, 77, 32, "?");

	write_passwd("ghost:*::0::/:/bin/sh\n"
	    "huge:*:99999999999:0::/:/bin/sh\n"
	    "mixed:*:1a:0::/:/bin/sh\n"
	    "root:*:0:0::/:/bin/sh\n");
	expect("empty uid field does not match uid 0", path, 0, 32, "root");
	expect("overflowing uid does not match", path, 1215752191U, 32, "?");
	expect("non-digit uid does not match", path, 1, 32, "?");

	write_passwd("root:*:0:0::/:/bin/sh");
	expect("unterminated last record does not match", path, 0, 32, "?");

	expect("missing file stores ?", "/nonexistent/passwd", 0, 32, "?");

	unlink(path);
	rmdir(directory);
	if (failures != 0) {
		printf("pwname_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("pwname_test: PASS\n");
	return 0;
}
