/*
 * cpio -- copy file archives in and out, POSIX odc format.
 *
 * The odc header is the portable ASCII format: a "070707" magic and ten
 * octal fields, 76 bytes in all, followed by the path including its
 * terminator and then the file's bytes, with no padding anywhere. Field
 * widths follow HD_CPIO in 4.4BSD-Lite2 bin/pax/cpio.h. The archive ends
 * with a header naming TRAILER!!! at length zero.
 *
 * Names arrive on standard input one per line for -o, which is how cpio
 * pairs with find(1), so this carries no directory walk of its own; -i
 * reads the archive on standard input.
 */
#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAGIC       "070707"
#define HEADER_SIZE 76
#define TRAILER     "TRAILER!!!"
#define BUFFER_SIZE 512
#ifndef CPIO_PATH_LIMIT
#define CPIO_PATH_LIMIT MAXPATHLEN
#endif

struct cpio_header {
    char magic[6];
    char device[6];
    char inode[6];
    char mode[6];
    char uid[6];
    char gid[6];
    char link_count[6];
    char special_device[6];
    char modification_time[11];
    char name_size[6];
    char file_size[11];
};

_Static_assert(sizeof(struct cpio_header) == HEADER_SIZE,
    "odc header must remain 76 bytes");
_Static_assert(CPIO_PATH_LIMIT <= MAXPATHLEN,
    "cpio path limit must fit the platform path limit");

static char archive_path[CPIO_PATH_LIMIT];
static char io_buffer[BUFFER_SIZE];
static int verbose_flag;
static int table_flag;

static void validate_archive_path(const char *name);
static char *enter_parent_directories(int root_descriptor,
    int create_missing);

static _Noreturn void
fatal_message(const char *message)
{
    fprintf(stderr, "cpio: %s\n", message);
    exit(1);
}

static _Noreturn void
fatal_path(const char *operation, const char *path)
{
    int saved_errno = errno;

    fprintf(stderr, "cpio: %s %s: ", operation, path);
    errno = saved_errno;
    perror("");
    exit(1);
}

static int
put_octal_field(char *field, size_t width, unsigned long value)
{
    size_t index = width;

    while (index > 0) {
        index--;
        field[index] = (char)('0' + (value & 7UL));
        value >>= 3;
    }
    return value == 0 ? 0 : -1;
}

static int
parse_octal_field(const char *field, size_t width, unsigned long *result)
{
    unsigned long value = 0;
    size_t index;

    for (index = 0; index < width; index++) {
        unsigned long digit;

        if (field[index] < '0' || field[index] > '7')
            return -1;
        digit = (unsigned long)(field[index] - '0');
        if (value > (ULONG_MAX - digit) / 8UL)
            return -1;
        value = value * 8UL + digit;
    }
    *result = value;
    return 0;
}

static int
write_all(int file_descriptor, const void *buffer, size_t byte_count)
{
    const char *position = buffer;

    while (byte_count > 0) {
        ssize_t written = write(file_descriptor, position, byte_count);

        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        position += written;
        byte_count -= (size_t)written;
    }
    return 0;
}

static size_t
read_full(int file_descriptor, void *buffer, size_t byte_count)
{
    char *position = buffer;
    size_t total = 0;

    while (total < byte_count) {
        ssize_t received = read(file_descriptor, position + total,
            byte_count - total);

        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0)
            break;
        total += (size_t)received;
    }
    return total;
}

static void
write_header(const struct stat *status, const char *name, size_t name_length)
{
    struct cpio_header header;

    memcpy(header.magic, MAGIC, sizeof(header.magic));
    (void)put_octal_field(header.device, sizeof(header.device),
        (unsigned long)status->st_dev);
    (void)put_octal_field(header.inode, sizeof(header.inode),
        (unsigned long)status->st_ino);
    (void)put_octal_field(header.mode, sizeof(header.mode),
        (unsigned long)status->st_mode);
    (void)put_octal_field(header.uid, sizeof(header.uid),
        (unsigned long)status->st_uid);
    (void)put_octal_field(header.gid, sizeof(header.gid),
        (unsigned long)status->st_gid);
    (void)put_octal_field(header.link_count, sizeof(header.link_count),
        (unsigned long)status->st_nlink);
    (void)put_octal_field(header.special_device, sizeof(header.special_device),
        (unsigned long)status->st_rdev);
    (void)put_octal_field(header.modification_time,
        sizeof(header.modification_time), (unsigned long)status->st_mtime);
    if (status->st_size < 0 ||
        put_octal_field(header.name_size, sizeof(header.name_size),
        (unsigned long)name_length) < 0 ||
        put_octal_field(header.file_size, sizeof(header.file_size),
        (unsigned long)status->st_size) < 0)
        fatal_message("file metadata exceeds the odc format");
    if (write_all(STDOUT_FILENO, &header, sizeof(header)) < 0 ||
        write_all(STDOUT_FILENO, name, name_length) < 0)
        fatal_path("write archive", "stdout");
}

static void
copy_file_to_archive(int input_descriptor, long file_size)
{
    long remaining = file_size;

    while (remaining > 0) {
        size_t requested = remaining < BUFFER_SIZE ?
            (size_t)remaining : BUFFER_SIZE;
        ssize_t received = read(input_descriptor, io_buffer, requested);

        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0)
            fatal_message("input file changed during archive creation");
        if (write_all(STDOUT_FILENO, io_buffer, (size_t)received) < 0)
            fatal_path("write archive", "stdout");
        remaining -= received;
    }
}

static int
read_archive_path(size_t *path_length)
{
    size_t length = 0;

    for (;;) {
        int byte = fgetc(stdin);

        if (byte == EOF) {
            if (ferror(stdin))
                fatal_path("read path list", "stdin");
            if (length == 0)
                return 0;
            break;
        }
        if (byte == '\n')
            break;
        if (byte == '\0')
            fatal_message("input path contains a NUL byte");
        if (length == sizeof(archive_path) - 1)
            fatal_message("input path exceeds cpio path limit");
        archive_path[length++] = (char)byte;
    }
    archive_path[length] = '\0';
    *path_length = length;
    return 1;
}

static void
copy_out(void)
{
    struct stat status;
    size_t path_length;
    int root_descriptor = open(".", O_RDONLY);

    if (root_descriptor < 0)
        fatal_path("open archive root", ".");

    while (read_archive_path(&path_length)) {
        char *leaf_name;
        dev_t path_device;
        ino_t path_inode;
        mode_t path_mode;
        int input_descriptor;

        if (path_length == 0)
            continue;
        validate_archive_path(archive_path);
        leaf_name = enter_parent_directories(root_descriptor, 0);
        if (lstat(leaf_name, &status) < 0)
            fatal_path("lstat", archive_path);
        if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)) {
            fprintf(stderr, "cpio: %s: unsupported file type\n",
                archive_path);
            exit(1);
        }
        path_device = status.st_dev;
        path_inode = status.st_ino;
        path_mode = status.st_mode;
        input_descriptor = open(leaf_name, O_RDONLY);
        if (input_descriptor < 0)
            fatal_path("open", archive_path);
        if (fstat(input_descriptor, &status) < 0)
            fatal_path("fstat", archive_path);
        if (status.st_dev != path_device || status.st_ino != path_inode ||
            (status.st_mode & S_IFMT) != (path_mode & S_IFMT))
            fatal_message("input path changed during archive creation");
        if (S_ISDIR(status.st_mode))
            status.st_size = 0;
        write_header(&status, archive_path, path_length + 1);
        if (verbose_flag)
            fprintf(stderr, "%s\n", archive_path);
        if (S_ISREG(status.st_mode))
            copy_file_to_archive(input_descriptor, status.st_size);
        if (close(input_descriptor) < 0)
            fatal_path("close", archive_path);
    }
    memset(&status, 0, sizeof(status));
    write_header(&status, TRAILER, sizeof(TRAILER));
    if (close(root_descriptor) < 0)
        fatal_path("close archive root", ".");
}

static void
open_verified_directory(const char *name, mode_t creation_mode,
    int enter_directory, int create_missing)
{
    struct stat status;
    dev_t path_device;
    ino_t path_inode;
    mode_t creation_mask = 0;
    int created_directory = 0;
    int directory_descriptor;

    if (lstat(name, &status) < 0) {
        if (errno != ENOENT)
            fatal_path("lstat", name);
        if (!create_missing)
            fatal_path("lstat archive parent", archive_path);
        /* A later entry may descend through this streaming directory entry.
         * Owner access keeps that path usable without retaining an unbounded
         * list of directories for a second metadata pass.
         */
        creation_mask = umask(0);
        if (mkdir(name, S_IRWXU) < 0) {
            int saved_errno = errno;

            (void)umask(creation_mask);
            errno = saved_errno;
            fatal_path("mkdir", name);
        }
        (void)umask(creation_mask);
        created_directory = 1;
        if (lstat(name, &status) < 0)
            fatal_path("lstat created directory", name);
    }
    if (!S_ISDIR(status.st_mode))
        fatal_message("path contains a non-directory component");
    path_device = status.st_dev;
    path_inode = status.st_ino;
    directory_descriptor = open(name, O_RDONLY);
    if (directory_descriptor < 0)
        fatal_path("open directory", name);
    if (fstat(directory_descriptor, &status) < 0)
        fatal_path("fstat directory", name);
    if (!S_ISDIR(status.st_mode) || status.st_dev != path_device ||
        status.st_ino != path_inode)
        fatal_message("directory changed during traversal");
    if (created_directory &&
        fchmod(directory_descriptor,
        ((creation_mode & 0777) & ~creation_mask) | S_IRWXU) < 0)
        fatal_path("set created directory mode", name);
    if (enter_directory && fchdir(directory_descriptor) < 0)
        fatal_path("enter directory", name);
    if (close(directory_descriptor) < 0)
        fatal_path("close directory", name);
}

static void
validate_archive_path(const char *name)
{
    const char *component = name;
    const char *cursor;

    if (name[0] == '\0' || name[0] == '/')
        fatal_message("archive contains an unsafe path");
    for (cursor = name;; cursor++) {
        size_t component_length;
        unsigned char byte = (unsigned char)*cursor;

        if (byte < 0x20U || (byte >= 0x7fU && byte <= 0x9fU)) {
            if (byte == '\0')
                break;
            fatal_message("archive path contains a control byte");
        }

        if (*cursor != '/')
            continue;
        component_length = (size_t)(cursor - component);
        if (component_length == 0 ||
            (component_length == 1 && component[0] == '.') ||
            (component_length == 2 && component[0] == '.' &&
            component[1] == '.'))
            fatal_message("archive contains an unsafe path component");
        component = cursor + 1;
    }
    if (cursor == component ||
        (cursor - component == 1 && component[0] == '.') ||
        (cursor - component == 2 && component[0] == '.' &&
        component[1] == '.'))
        fatal_message("archive contains an unsafe path component");
}

static char *
enter_parent_directories(int root_descriptor, int create_missing)
{
    char *component = archive_path;
    char *cursor;

    if (fchdir(root_descriptor) < 0)
        fatal_path("restore extraction root", ".");
    for (cursor = archive_path; *cursor != '\0'; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        open_verified_directory(component, 0777, 1, create_missing);
        *cursor = '/';
        component = cursor + 1;
    }
    return component;
}

static int
create_output_file(const char *leaf_name, mode_t mode)
{
    int descriptor = open(leaf_name, O_WRONLY | O_CREAT | O_EXCL,
        mode & 0777);

    if (descriptor < 0)
        fatal_path("create new output", archive_path);
    return descriptor;
}

static void
copy_archive_data(unsigned long file_size, int output_descriptor,
    const char *leaf_name)
{
    unsigned long remaining = file_size;

    while (remaining > 0) {
        size_t requested = remaining < BUFFER_SIZE ?
            (size_t)remaining : BUFFER_SIZE;
        size_t received = read_full(STDIN_FILENO, io_buffer, requested);

        if (received != requested) {
            if (output_descriptor >= 0) {
                (void)close(output_descriptor);
                (void)unlink(leaf_name);
            }
            fatal_message("truncated file data");
        }
        if (output_descriptor >= 0 &&
            write_all(output_descriptor, io_buffer, received) < 0) {
            int saved_errno = errno;

            (void)close(output_descriptor);
            (void)unlink(leaf_name);
            errno = saved_errno;
            fatal_path("write output", archive_path);
        }
        remaining -= received;
    }
}

static void
copy_in(void)
{
    int root_descriptor = -1;

    if (!table_flag) {
        root_descriptor = open(".", O_RDONLY);
        if (root_descriptor < 0)
            fatal_path("open extraction root", ".");
    }
    for (;;) {
        struct cpio_header header;
        unsigned long name_size;
        unsigned long file_size;
        unsigned long mode_value;
        size_t header_bytes = read_full(STDIN_FILENO, &header,
            sizeof(header));
        char *leaf_name;
        int output_descriptor = -1;

        if (header_bytes == 0)
            fatal_message("archive ended before TRAILER!!!");
        if (header_bytes != sizeof(header))
            fatal_message("truncated header");
        if (memcmp(header.magic, MAGIC, sizeof(header.magic)) != 0)
            fatal_message("not an odc archive");
        if (parse_octal_field(header.name_size, sizeof(header.name_size),
            &name_size) < 0 || name_size == 0 ||
            name_size > sizeof(archive_path))
            fatal_message("bad name length");
        if (read_full(STDIN_FILENO, archive_path, (size_t)name_size) !=
            (size_t)name_size)
            fatal_message("truncated name");
        if (archive_path[name_size - 1] != '\0' ||
            memchr(archive_path, '\0', (size_t)name_size - 1) != NULL)
            fatal_message("archive name lacks one trailing terminator");
        if (parse_octal_field(header.file_size, sizeof(header.file_size),
            &file_size) < 0 || file_size > LONG_MAX)
            fatal_message("bad file size");
        if (parse_octal_field(header.mode, sizeof(header.mode),
            &mode_value) < 0 ||
            (unsigned long)(mode_t)mode_value != mode_value)
            fatal_message("bad file mode");
        if (strcmp(archive_path, TRAILER) == 0) {
            if (file_size != 0)
                fatal_message("TRAILER!!! carries file data");
            if (root_descriptor >= 0 && close(root_descriptor) < 0)
                fatal_path("close extraction root", ".");
            return;
        }

        validate_archive_path(archive_path);
        if (S_ISDIR((mode_t)mode_value)) {
            if (file_size != 0)
                fatal_message("directory entry carries file data");
        } else if (!S_ISREG((mode_t)mode_value)) {
            fatal_message("archive contains an unsupported file type");
        }
        if (table_flag) {
            printf("%s\n", archive_path);
        } else {
            leaf_name = enter_parent_directories(root_descriptor, 1);
            if (S_ISDIR((mode_t)mode_value)) {
                open_verified_directory(leaf_name,
                    (mode_t)mode_value & 07777, 0, 1);
            } else {
                output_descriptor = create_output_file(leaf_name,
                    (mode_t)mode_value);
            }
            if (verbose_flag)
                fprintf(stderr, "%s\n", archive_path);
        }
        leaf_name = strrchr(archive_path, '/');
        leaf_name = leaf_name == NULL ? archive_path : leaf_name + 1;
        copy_archive_data(file_size, output_descriptor, leaf_name);
        if (output_descriptor >= 0 && close(output_descriptor) < 0) {
            int saved_errno = errno;

            (void)unlink(leaf_name);
            errno = saved_errno;
            fatal_path("close output", archive_path);
        }
    }
}

int
main(int argc, char **argv)
{
    void (*volatile archive_operation)(void);
    int output_mode = 0;
    int input_mode = 0;
    int argument_index;

    for (argument_index = 1; argument_index < argc; argument_index++) {
        const char *option = argv[argument_index];

        if (*option == '-')
            option++;
        for (; *option != '\0'; option++) {
            switch (*option) {
            case 'o':
                output_mode++;
                break;
            case 'i':
                input_mode++;
                break;
            case 't':
                table_flag++;
                break;
            case 'd':
                break;
            case 'v':
                verbose_flag++;
                break;
            default:
                fprintf(stderr, "cpio: %c: unknown option\n", *option);
                return 1;
            }
        }
    }
    if ((output_mode == 0) == (input_mode == 0)) {
        fprintf(stderr, "cpio: usage: cpio -o[v] | cpio -i[tvd]\n");
        return 1;
    }
    /* Indirect dispatch keeps the mutually exclusive archive-mode frames
     * out of main's constrained process stack.
     */
    archive_operation = output_mode != 0 ? copy_out : copy_in;
    archive_operation();
    return 0;
}
