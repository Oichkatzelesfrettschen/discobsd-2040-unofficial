/*
 * Host fault boundary for the production v7fs descriptor backend.
 * Only v7fs_io_fd.c is compiled with renamed read/write/lseek calls. The
 * core and formatter retain their production entry points and fixed pool.
 * Callback errors use negative errno values; each successful callback
 * returns its actual transferred-byte count.
 */
#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "v7fs.h"
#include "v7fs_impl.h"
#include "v7fs_endian.h"
#include "v7fs_inode.h"
#include "v7fs_file.h"
#include "v7fs_datablock.h"
#include "v7fs_superblock.h"
#include "newfs_v7fs.h"

typedef int64_t (*transfer_callback)(int, void *, size_t, int);
typedef int64_t (*seek_callback)(int, int64_t, int);

static transfer_callback transfer;
static seek_callback seek_position;
static struct v7fs_self *filesystem;
static struct v7fs_mount_device mount_device;

int v7fs_newfs_verbose;

void
progress(const struct progress_arg *argument)
{
    (void)argument;
}

void
progress_done(void)
{
}

void
storage_backend_close(void)
{
    if (filesystem != NULL) {
        v7fs_io_fini(filesystem);
        filesystem = NULL;
    }
}

void
storage_backend_callbacks(transfer_callback transfer_function, seek_callback seek_function)
{
    storage_backend_close();
    transfer = transfer_function;
    seek_position = seek_function;
}

static ssize_t
transfer_bytes(int descriptor, void *buffer, size_t length, int writing)
{
    int64_t result;

    if (transfer == NULL) {
        errno = EIO;
        return -1;
    }
    result = transfer(descriptor, buffer, length, writing);
    if (result < 0 || (uint64_t)result > length) {
        errno = result < 0 && result >= -INT_MAX ? (int)-result : EIO;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t
storage_test_read(int descriptor, void *buffer, size_t length)
{
    return transfer_bytes(descriptor, buffer, length, 0);
}

ssize_t
storage_test_write(int descriptor, const void *buffer, size_t length)
{
    return transfer_bytes(descriptor, (void *)buffer, length, 1);
}

off_t
storage_test_seek(int descriptor, off_t offset, int whence)
{
    int64_t result;

    if (seek_position == NULL) {
        errno = EIO;
        return (off_t)-1;
    }
    result = seek_position(descriptor, offset, whence);
    if (result < 0) {
        errno = result >= -INT_MAX ? (int)-result : EIO;
        return (off_t)-1;
    }
    return (off_t)result;
}

int
storage_backend_open(unsigned sectors, int pdp_order)
{
    storage_backend_close();
    if (sectors == 0 || sectors > INT_MAX)
        return EINVAL;
    memset(&mount_device, 0, sizeof(mount_device));
    mount_device.device.fd = 0;
    mount_device.sectors = (daddr_t)sectors;
    mount_device.endian = pdp_order ? PDP_ENDIAN : LITTLE_ENDIAN;
    return v7fs_io_init(&filesystem, &mount_device, V7FS_BSIZE);
}

int
storage_backend_mount(void)
{
    if (filesystem == NULL)
        return EINVAL;
    return v7fs_superblock_load(filesystem);
}

int
storage_backend_format(int32_t inodes)
{
    int error;

    storage_backend_close();
    error = v7fs_newfs(&mount_device, inodes);
    if (error != 0)
        return error;
    error = v7fs_io_init(&filesystem, &mount_device, V7FS_BSIZE);
    return error != 0 ? error : storage_backend_mount();
}

int
storage_backend_read(int32_t sector, uint8_t *buffer)
{
    return filesystem != NULL && filesystem->io.read(filesystem->io.cookie, buffer, sector);
}

int
storage_backend_write(int32_t sector, uint8_t *buffer)
{
    return filesystem != NULL && filesystem->io.write(filesystem->io.cookie, buffer, sector);
}

int
storage_backend_sync(void)
{
    return filesystem != NULL ? v7fs_superblock_writeback(filesystem) : EINVAL;
}

int
storage_backend_create(const char *name, int directory)
{
    struct v7fs_inode parent;
    struct v7fs_fileattr attributes;
    v7fs_ino_t inode;
    int error;

    if (filesystem == NULL)
        return EINVAL;
    error = v7fs_inode_load(filesystem, &parent, V7FS_ROOT_INODE);
    if (error != 0)
        return error;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mode = directory ? V7FS_IFDIR | 0755 : V7FS_IFREG | 0644;
    attributes.atime = attributes.mtime = attributes.ctime = 1000000000;
    return v7fs_file_allocate(filesystem, &parent, name, strlen(name), &attributes, &inode);
}

int
storage_backend_create_at(unsigned parent_number, const char *name, int directory)
{
    struct v7fs_inode parent;
    struct v7fs_fileattr attributes;
    v7fs_ino_t inode;
    int error;

    if (filesystem == NULL)
        return -EINVAL;
    error = v7fs_inode_load(filesystem, &parent, (v7fs_ino_t)parent_number);
    if (error != 0)
        return -error;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mode = directory ? V7FS_IFDIR | 0755 : V7FS_IFREG | 0644;
    attributes.atime = attributes.mtime = attributes.ctime = 1000000000;
    error = v7fs_file_allocate(filesystem, &parent, name, strlen(name),
        &attributes, &inode);
    return error != 0 ? -error : (int)inode;
}

int
storage_backend_lookup(const char *name)
{
    struct v7fs_inode parent;
    v7fs_ino_t inode;
    int error;

    if (filesystem == NULL)
        return -EINVAL;
    error = v7fs_inode_load(filesystem, &parent, V7FS_ROOT_INODE);
    if (error == 0)
        error = v7fs_file_lookup_by_name(filesystem, &parent, name, strlen(name), &inode);
    return error != 0 ? -error : (int)inode;
}

int
storage_backend_lookup_at(unsigned parent_number, const char *name)
{
    struct v7fs_inode parent;
    v7fs_ino_t inode;
    int error;

    if (filesystem == NULL)
        return -EINVAL;
    error = v7fs_inode_load(filesystem, &parent, (v7fs_ino_t)parent_number);
    if (error == 0)
        error = v7fs_file_lookup_by_name(filesystem, &parent, name, strlen(name), &inode);
    return error != 0 ? -error : (int)inode;
}

int
storage_backend_reclaim(unsigned number)
{
    struct v7fs_inode inode;
    int error;

    if (filesystem == NULL || number <= V7FS_ROOT_INODE || number > UINT16_MAX)
        return EINVAL;
    error = v7fs_inode_load(filesystem, &inode, (v7fs_ino_t)number);
    if (error != 0)
        return error;
    if (inode.mode == 0 || v7fs_inode_nlink(&inode) != 0)
        return EINVAL;
    /* The host caller holds no open references to the displaced inode. */
    error = v7fs_datablock_contract(filesystem, &inode, inode.filesize);
    return error != 0 ? error : v7fs_inode_deallocate(filesystem, (v7fs_ino_t)number);
}

int
storage_backend_rename(unsigned source_parent, const char *source,
    unsigned target_parent, const char *target)
{
    struct v7fs_inode source_directory, target_directory;
    int error;

    if (filesystem == NULL)
        return EINVAL;
    error = v7fs_inode_load(filesystem, &source_directory,
        (v7fs_ino_t)source_parent);
    if (error != 0)
        return error;
    if (source_parent == target_parent)
        return v7fs_file_rename(filesystem, &source_directory, source,
            strlen(source), &source_directory, target, strlen(target));
    error = v7fs_inode_load(filesystem, &target_directory,
        (v7fs_ino_t)target_parent);
    return error != 0 ? error : v7fs_file_rename(filesystem,
        &source_directory, source, strlen(source), &target_directory, target,
        strlen(target));
}

int
storage_backend_link(const char *source, const char *name)
{
    struct v7fs_inode parent, inode;
    int number, error;

    number = storage_backend_lookup(source);
    if (number < 0)
        return -number;
    error = v7fs_inode_load(filesystem, &parent, V7FS_ROOT_INODE);
    if (error == 0)
        error = v7fs_inode_load(filesystem, &inode, (v7fs_ino_t)number);
    return error != 0 ? error : v7fs_file_link(filesystem, &parent, &inode, name, strlen(name));
}

struct directory_fill {
    unsigned slot;
    v7fs_ino_t target;
};

static int
fill_directory_block(struct v7fs_self *self, void *context, v7fs_daddr_t block, size_t length)
{
    struct directory_fill *fill = context;
    uint8_t buffer[V7FS_BSIZE];
    uint16_t disk_inode;
    unsigned slot;
    char *name;

    if (length != sizeof(buffer))
        return V7FS_ITERATOR_ERROR;
    memset(buffer, 0, sizeof(buffer));
    for (slot = 0; slot < V7FS_BSIZE / 16; slot++, fill->slot++) {
        disk_inode = V7FS_VAL16(self, fill->slot < 2 ? V7FS_ROOT_INODE : fill->target);
        memcpy(buffer + slot * 16, &disk_inode, sizeof(disk_inode));
        name = (char *)(buffer + slot * 16 + 2);
        if (fill->slot < 2)
            strcpy(name, fill->slot == 0 ? "." : "..");
        else if (fill->slot == 2)
            strcpy(name, "seed");
        else
            (void)snprintf(name, V7FS_NAME_MAX, "f%06u", fill->slot);
    }
    return self->io.write(self->io.cookie, buffer, block) ? 0 : V7FS_ITERATOR_ERROR;
}

/* Construct a full parent with many valid hard links to one empty file. */
int
storage_backend_fill_parent(unsigned blocks)
{
    struct v7fs_inode parent, target;
    struct directory_fill fill;
    int error, number;
    unsigned bytes;

    if (blocks == 0 || blocks > 138)
        return EINVAL;
    error = storage_backend_create("seed", 0);
    if (error != 0)
        return error;
    number = storage_backend_lookup("seed");
    if (number < 0)
        return -number;
    error = v7fs_inode_load(filesystem, &parent, V7FS_ROOT_INODE);
    if (error == 0)
        error = v7fs_inode_load(filesystem, &target, (v7fs_ino_t)number);
    if (error != 0)
        return error;
    bytes = blocks * V7FS_BSIZE;
    error = v7fs_datablock_expand(filesystem, &parent, bytes - parent.filesize);
    if (error != 0)
        return error;
    fill.slot = 0;
    fill.target = target.inode_number;
    if (v7fs_datablock_foreach(filesystem, &parent, fill_directory_block, &fill) !=
        V7FS_ITERATOR_END)
        return EIO;
    target.nlink = (int16_t)(fill.slot - 2);
    error = v7fs_inode_writeback(filesystem, &target);
    if (error == 0)
        error = v7fs_inode_writeback(filesystem, &parent);
    return error != 0 ? error : storage_backend_sync();
}
