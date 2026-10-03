/* Generate bounded ustar inputs for the tar security regression gate. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_SIZE 512U

struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char major[8];
    char minor[8];
    char prefix[155];
    char padding[12];
};

_Static_assert(sizeof(struct ustar_header) == RECORD_SIZE,
    "ustar header must occupy one record");

static int malformed_size_field;

static void
fail(const char *operation)
{
    int saved_errno = errno;

    fprintf(stderr, "tarfixture: %s: ", operation);
    errno = saved_errno;
    perror("");
    exit(1);
}

static void
write_all(const void *buffer, size_t length)
{
    if (fwrite(buffer, 1, length, stdout) != length)
        fail("write archive");
}

static void
put_octal(char *field, size_t width, unsigned long value)
{
    size_t index = width - 1;

    field[index] = '\0';
    while (index > 0) {
        index--;
        field[index] = (char)('0' + (value & 7UL));
        value >>= 3;
    }
    if (value != 0) {
        errno = ERANGE;
        fail("encode octal field");
    }
}

static void
write_entry(const char *name, char type, const char *link_target,
    unsigned long mode, const char *contents)
{
    struct ustar_header header;
    unsigned char *bytes = (unsigned char *)&header;
    unsigned long checksum = 0;
    size_t name_length = strlen(name);
    size_t content_length = contents == NULL ? 0 : strlen(contents);
    size_t index;
    char padding[RECORD_SIZE] = {0};

    if (name_length > sizeof(header.name) ||
        (link_target != NULL && strlen(link_target) > sizeof(header.linkname))) {
        errno = ENAMETOOLONG;
        fail("fixture path");
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.name, name, name_length);
    if (link_target != NULL)
        memcpy(header.linkname, link_target, strlen(link_target));
    put_octal(header.mode, sizeof(header.mode), mode);
    put_octal(header.uid, sizeof(header.uid), 0);
    put_octal(header.gid, sizeof(header.gid), 0);
    put_octal(header.size, sizeof(header.size), content_length);
    if (malformed_size_field)
        header.size[0] = '8';
    put_octal(header.mtime, sizeof(header.mtime), 0);
    memset(header.checksum, ' ', sizeof(header.checksum));
    header.type = type;
    memcpy(header.magic, "ustar", 5);
    memcpy(header.version, "00", 2);
    for (index = 0; index < sizeof(header); index++)
        checksum += bytes[index];
    (void)snprintf(header.checksum, sizeof(header.checksum), "%06lo", checksum);
    header.checksum[6] = '\0';
    header.checksum[7] = ' ';
    write_all(&header, sizeof(header));
    if (content_length > 0) {
        size_t pad_length = RECORD_SIZE - content_length % RECORD_SIZE;

        write_all(contents, content_length);
        if (pad_length != RECORD_SIZE)
            write_all(padding, pad_length);
    }
}

static void
write_end(void)
{
    char records[RECORD_SIZE * 2] = {0};

    write_all(records, sizeof(records));
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        char records[RECORD_SIZE * 8] = {0};

        malformed_size_field = 1;
        write_entry("bad-size", '0', NULL, 0644, NULL);
        for (;;)
            write_all(records, sizeof(records));
    }
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: tarfixture scenario [absolute-path]\n");
        return 1;
    }
    if (strcmp(argv[1], "dotdot") == 0) {
        write_entry("../escaped", '0', NULL, 0644, "archive payload\n");
    } else if (strcmp(argv[1], "absolute") == 0) {
        if (argc != 3 || argv[2][0] != '/') {
            fprintf(stderr, "tarfixture: absolute scenario needs a path\n");
            return 1;
        }
        write_entry(argv[2], '0', NULL, 0644, "archive payload\n");
    } else if (strcmp(argv[1], "dot") == 0) {
        write_entry("./dot", '0', NULL, 0644, "archive payload\n");
    } else if (strcmp(argv[1], "empty-component") == 0) {
        write_entry("directory//file", '0', NULL, 0644,
            "archive payload\n");
    } else if (strcmp(argv[1], "control") == 0) {
        write_entry("line\nbreak", '0', NULL, 0644, "archive payload\n");
    } else if (strcmp(argv[1], "symlink-control") == 0) {
        write_entry("link", '2', "target\033control", 0777, NULL);
    } else if (strcmp(argv[1], "hardlink-control") == 0) {
        write_entry("link", '1', "target\ncontrol", 0644, NULL);
    } else if (strcmp(argv[1], "archive-symlink") == 0) {
        write_entry("pivot", '2', "inside", 0777, NULL);
        write_entry("pivot/from-archive", '0', NULL, 0644,
            "archive payload\n");
    } else if (strcmp(argv[1], "symlink-mid-dotdot") == 0) {
        write_entry("link", '2', "named/../target", 0777, NULL);
    } else if (strcmp(argv[1], "final") == 0) {
        write_entry("victim", '0', NULL, 0644, "archive payload\n");
    } else if (strcmp(argv[1], "hardlink") == 0) {
        write_entry("inside-link", '1', "../outside-existing", 0644, NULL);
    } else if (strcmp(argv[1], "hardlink-source") == 0) {
        write_entry("inside-link", '1', "source", 0644, NULL);
    } else if (strcmp(argv[1], "special") == 0) {
        write_entry("setid", '0', NULL, 06755, "payload\n");
    } else if (strcmp(argv[1], "unsupported") == 0) {
        write_entry("device", '3', NULL, 0600, NULL);
    } else if (strcmp(argv[1], "directory-data") == 0) {
        write_entry("directory", '5', NULL, 0755, "payload\n");
    } else if (strcmp(argv[1], "zero-directories") == 0) {
        write_entry("tree", '5', NULL, 0000, NULL);
        write_entry("tree/sub", '5', NULL, 0000, NULL);
        write_entry("tree/sub/file", '0', NULL, 0600, "payload\n");
    } else if (strcmp(argv[1], "repeated-hardlink") == 0) {
        write_entry("source", '0', NULL, 0644, "old payload\n");
        write_entry("alias", '1', "source", 0644, NULL);
        write_entry("source", '0', NULL, 0644, "new payload\n");
        write_entry("alias2", '1', "alias", 0644, NULL);
    } else if (strcmp(argv[1], "repeated-symlink") == 0) {
        write_entry("link", '2', "first", 0777, NULL);
        write_entry("link", '2', "second", 0777, NULL);
    } else if (strcmp(argv[1], "directory-revisit") == 0) {
        write_entry("locked", '5', NULL, 0000, NULL);
        write_entry("other", '5', NULL, 0700, NULL);
        write_entry("locked/file", '0', NULL, 0600, "payload\n");
    } else if (strcmp(argv[1], "implicit-parent") == 0) {
        write_entry("implicit/nested/file", '0', NULL, 0600,
            "payload\n");
    } else if (strcmp(argv[1], "bad-octal") == 0) {
        malformed_size_field = 1;
        write_entry("bad-size", '0', NULL, 0644, NULL);
    } else {
        fprintf(stderr, "tarfixture: unknown scenario: %s\n", argv[1]);
        return 1;
    }
    write_end();
    return fflush(stdout) == EOF ? 1 : 0;
}
