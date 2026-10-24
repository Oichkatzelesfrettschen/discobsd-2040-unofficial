/*
 * ILP32 contract for descriptor-owned staged publication through the
 * production restricted pathname walker and UFS directory writers.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

#define ROOT_DESCRIPTOR 3
#define STAGED_DESCRIPTOR 4
#define TARGET_DESCRIPTOR 5

struct user u;
struct timeval time;
struct mount mount[NMOUNT];
struct inode *rootdir;
struct namecache namecache[NNAMECACHE];

void publishat_beneath(void);

static struct inode root_inode;
static struct inode staged_inode;
static struct inode target_inode;
static struct inode replacement_inode;
static struct inode symlink_inode;
static struct file root_file;
static struct file staged_file;
static struct file target_file;
static union {
    struct direct entry;
    char bytes[DIRBLKSIZ];
} directory_storage;
static union {
    struct direct entry;
    char bytes[DIRBLKSIZ];
} directory_block_storage;
static struct buf directory_buffer;

static int namespace_probe;
static int release_gate_during_wait;
static int replace_stage_during_wait;
static int replace_target_during_wait;
static int release_staged_descriptor_during_lookup;
static int wait_error;
static int directory_buffer_write_error;
static unsigned descriptor_lookups;
static unsigned inode_get_calls;
static unsigned directory_block_reads;
static unsigned directory_buffer_releases;
static unsigned directory_buffer_writes;
static unsigned directory_record_writes;
static unsigned wait_calls;

static int namespace_is_owned(void);

static void
copy_bytes(caddr_t destination, const caddr_t source, u_int length)
{
    u_int index;

    for (index = 0; index < length; index++)
        destination[index] = source[index];
}

int
copyin(const caddr_t source, caddr_t destination, u_int length)
{
    if ((u_long)source < 4096)
        return EFAULT;
    copy_bytes(destination, source, length);
    return 0;
}

int
copystr(caddr_t source, caddr_t destination, u_int maximum, u_int *copied)
{
    u_int length;

    for (length = 0; length < maximum; length++) {
        destination[length] = source[length];
        if (source[length] == '\0') {
            if (copied != NULL)
                *copied = length + 1;
            return 0;
        }
    }
    if (copied != NULL)
        *copied = maximum;
    return ENOENT;
}

struct file *
getf(int descriptor)
{
    descriptor_lookups++;
    HK_CHECK(namespace_is_owned());
    if ((unsigned)descriptor >= NOFILE || u.u_ofile[descriptor] == NULL) {
        u.u_error = EBADF;
        return NULL;
    }
    return u.u_ofile[descriptor];
}

void
ilock(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    HK_CHECK((inode->i_flag & ILOCKED) == 0);
    inode->i_flag |= ILOCKED;
}

void
iunlock(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    HK_CHECK((inode->i_flag & ILOCKED) != 0);
    inode->i_flag &= ~ILOCKED;
}

void
irele(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    HK_CHECK(inode->i_count != 0);
    inode->i_count--;
}

void
iput(struct inode *inode)
{
    if (inode->i_flag & ILOCKED)
        iunlock(inode);
    irele(inode);
}

void
igrab(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    ilock(inode);
    inode->i_count++;
}

int
iupdat(struct inode *inode, struct timeval *access_time,
    struct timeval *modify_time, int waitfor)
{
    HK_CHECK(inode == &staged_inode);
    HK_CHECK(access_time == &time);
    HK_CHECK(modify_time == &time);
    HK_CHECK(waitfor == 1);
    return 0;
}

int
access(struct inode *inode, int mode)
{
    HK_CHECK(inode != NULL);
    HK_CHECK(mode == IEXEC || mode == IWRITE);
    return 0;
}

void
wakeup(caddr_t channel)
{
    HK_CHECK(channel != NULL);
}

static struct direct *
find_entry(const char *name)
{
    unsigned offset = 0;

    while (offset < DIRBLKSIZ) {
        struct direct *entry =
            (struct direct *)&directory_storage.bytes[offset];

        HK_CHECK(entry->d_reclen != 0);
        if (entry->d_reclen == 0)
            return NULL;
        if (entry->d_ino != 0 && hk_streq(entry->d_name, name))
            return entry;
        offset += entry->d_reclen;
    }
    HK_CHECK(offset == DIRBLKSIZ);
    return NULL;
}

int
tsleep(caddr_t channel, int priority, u_int timeout)
{
    struct direct *entry;

    HK_CHECK(channel != NULL);
    HK_CHECK(priority == (PINOD | PCATCH));
    HK_CHECK(timeout == 0);
    wait_calls++;
    if (namespace_probe)
        return EINTR;
    if (release_gate_during_wait) {
        if (replace_stage_during_wait) {
            entry = find_entry("stage");
            HK_CHECK(entry != NULL);
            if (entry != NULL)
                entry->d_ino = replacement_inode.i_number;
        }
        if (replace_target_during_wait) {
            entry = find_entry("target");
            HK_CHECK(entry != NULL);
            if (entry != NULL)
                entry->d_ino = replacement_inode.i_number;
        }
        release_gate_during_wait = 0;
        ufs_namespace_exit();
        return 0;
    }
    return wait_error;
}

static int
namespace_is_owned(void)
{
    int error;

    namespace_probe = 1;
    error = ufs_namespace_enter();
    namespace_probe = 0;
    if (error == 0) {
        ufs_namespace_exit();
        return 0;
    }
    return error == EINTR;
}

static void
check_namespace_released(void)
{
    HK_CHECK(!namespace_is_owned());
}

static struct inode *
inode_for_number(ino_t number)
{
    if (number == staged_inode.i_number)
        return &staged_inode;
    if (number == target_inode.i_number)
        return &target_inode;
    if (number == replacement_inode.i_number)
        return &replacement_inode;
    if (number == symlink_inode.i_number)
        return &symlink_inode;
    HK_CHECK(0);
    return NULL;
}

struct inode *
iget(dev_t device, struct fs *filesystem, ino_t number)
{
    struct inode *inode;

    inode_get_calls++;
    HK_CHECK(device == INODE_DEVICE(&root_inode));
    HK_CHECK(filesystem == INODE_FILESYSTEM(&root_inode));
    inode = inode_for_number(number);
    if (inode == NULL)
        return NULL;
    ilock(inode);
    inode->i_count++;
    return inode;
}

struct buf *
bread(dev_t device, daddr_t block)
{
    directory_block_reads++;
    HK_CHECK(device == INODE_DEVICE(&root_inode));
    HK_CHECK(block == 1);
    HK_CHECK(namespace_is_owned());
    if (release_staged_descriptor_during_lookup) {
        release_staged_descriptor_during_lookup = 0;
        u.u_ofile[STAGED_DESCRIPTOR] = NULL;
        staged_file.f_count = 0;
        irele(&staged_inode);
    }
    copy_bytes((caddr_t)directory_block_storage.bytes,
        (caddr_t)directory_storage.bytes, DIRBLKSIZ);
    directory_buffer = (struct buf){
        .b_addr = (caddr_t)directory_block_storage.bytes,
        .b_bcount = DIRBLKSIZ,
    };
    return &directory_buffer;
}

daddr_t
bmap(struct inode *inode, daddr_t block, int operation, int size)
{
    HK_CHECK(inode == &root_inode);
    HK_CHECK(block == 0);
    HK_CHECK(operation == B_READ);
    HK_CHECK(size == 0);
    return 1;
}

void
brelse(struct buf *buffer)
{
    HK_CHECK(buffer == &directory_buffer);
    directory_buffer_releases++;
}

int
bwrite(struct buf *buffer)
{
    HK_CHECK(buffer == &directory_buffer);
    HK_CHECK(namespace_is_owned());
    directory_buffer_writes++;
    if (directory_buffer_write_error != 0)
        return directory_buffer_write_error;
    copy_bytes((caddr_t)directory_storage.bytes,
        (caddr_t)directory_block_storage.bytes, DIRBLKSIZ);
    return 0;
}

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t bytes, int length,
    off_t offset, int flags, int *residual)
{
    HK_CHECK(namespace_is_owned());
    HK_CHECK(operation == UIO_WRITE);
    HK_CHECK(inode == &root_inode);
    HK_CHECK(length > 0);
    HK_CHECK(offset >= 0);
    HK_CHECK((u_long)offset + (u_long)length <= DIRBLKSIZ);
    HK_CHECK(flags == (IO_UNIT | IO_SYNC));
    HK_CHECK(residual == NULL);
    copy_bytes((caddr_t)&directory_storage.bytes[(unsigned)offset], bytes,
        (u_int)length);
    directory_record_writes++;
    return 0;
}

void
itrunc(struct inode *inode, off_t length, int flags)
{
    HK_CHECK(inode == &root_inode);
    HK_CHECK(length <= DIRBLKSIZ);
    HK_CHECK(flags == 0);
    inode->i_size = length;
}

static void
set_inode_location(struct inode *inode, dev_t device, struct fs *filesystem)
{
    inode->i_dev = device;
    inode->i_fs = filesystem;
}

static unsigned
write_entry(unsigned offset, const char *name, ino_t number,
    unsigned record_length)
{
    struct direct *entry =
        (struct direct *)&directory_storage.bytes[offset];
    unsigned name_length = 0;

    while (name[name_length] != '\0') {
        entry->d_name[name_length] = name[name_length];
        name_length++;
    }
    entry->d_name[name_length] = '\0';
    entry->d_namlen = name_length;
    entry->d_ino = number;
    if (record_length == 0)
        record_length = DIRSIZ(entry);
    entry->d_reclen = record_length;
    return record_length;
}

static void
configure_stage_only(ino_t stage_number)
{
    unsigned byte_index;

    for (byte_index = 0; byte_index < DIRBLKSIZ; byte_index++)
        directory_storage.bytes[byte_index] = 0;
    (void)write_entry(0, "stage", stage_number, DIRBLKSIZ);
}

static void
configure_stage_and_target(ino_t target_number)
{
    unsigned byte_index;
    unsigned stage_length;

    for (byte_index = 0; byte_index < DIRBLKSIZ; byte_index++)
        directory_storage.bytes[byte_index] = 0;
    stage_length = write_entry(0, "stage", staged_inode.i_number, 0);
    (void)write_entry(stage_length, "target", target_number,
        DIRBLKSIZ - stage_length);
}

static void
reset_fixture(void)
{
    unsigned cache_index;

    u = (struct user){ .u_uid = 20 };
    time = (struct timeval){ .tv_sec = 1234, .tv_usec = 0 };
    mount[0] = (struct mount){ .m_dev = 1 };
    mount[1] = (struct mount){ .m_dev = 2 };
    root_inode = (struct inode){
        .i_count = 1, .i_number = 2, .i_mode = IFDIR | 0777,
        .i_size = DIRBLKSIZ, .i_nlink = 2, .i_uid = 20,
    };
    staged_inode = (struct inode){
        .i_count = 1, .i_number = 10, .i_mode = IFREG | 0600,
        .i_nlink = 1, .i_uid = 20,
    };
    target_inode = (struct inode){
        .i_count = 1, .i_number = 11, .i_mode = IFREG | 0600,
        .i_nlink = 1, .i_uid = 20,
    };
    replacement_inode = (struct inode){
        .i_count = 1, .i_number = 12, .i_mode = IFREG | 0600,
        .i_nlink = 1, .i_uid = 20,
    };
    symlink_inode = (struct inode){
        .i_count = 1, .i_number = 13, .i_mode = IFLNK | 0777,
        .i_nlink = 1, .i_uid = 20,
    };
    set_inode_location(&root_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&staged_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&target_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&replacement_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&symlink_inode, mount[0].m_dev, &mount[0].m_filsys);
    root_file = (struct file){
        .f_count = 1, .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&root_inode,
    };
    staged_file = (struct file){
        .f_count = 1, .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&staged_inode,
    };
    target_file = (struct file){
        .f_count = 1, .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&target_inode,
    };
    u.u_ofile[ROOT_DESCRIPTOR] = &root_file;
    u.u_ofile[STAGED_DESCRIPTOR] = &staged_file;
    u.u_ofile[TARGET_DESCRIPTOR] = &target_file;
    rootdir = &root_inode;
    configure_stage_only(staged_inode.i_number);
    for (cache_index = 0; cache_index < NNAMECACHE; cache_index++)
        namecache[cache_index] = (struct namecache){0};
    directory_buffer = (struct buf){0};
    namespace_probe = 0;
    release_gate_during_wait = 0;
    replace_stage_during_wait = 0;
    replace_target_during_wait = 0;
    release_staged_descriptor_during_lookup = 0;
    wait_error = EINTR;
    directory_buffer_write_error = 0;
    descriptor_lookups = 0;
    inode_get_calls = 0;
    directory_block_reads = 0;
    directory_buffer_releases = 0;
    directory_buffer_writes = 0;
    directory_record_writes = 0;
    wait_calls = 0;
    nchstats = (struct nchstats){0};
}

static void
call_publishat(char *staged_path, int target_descriptor)
{
    struct {
        int root_descriptor;
        char *staged_path;
        int staged_descriptor;
        char *target_path;
        int target_descriptor;
    } arguments = {
        .root_descriptor = ROOT_DESCRIPTOR,
        .staged_path = staged_path,
        .staged_descriptor = STAGED_DESCRIPTOR,
        .target_path = "target",
        .target_descriptor = target_descriptor,
    };

    _Static_assert(sizeof(arguments) == sizeof(int) * 5,
        "publishat_beneath arguments must occupy five words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    publishat_beneath();
}

static void
absent_target_uses_production_enter_and_remove(void)
{
    struct direct *entry;

    reset_fixture();
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(find_entry("stage") == NULL);
    entry = find_entry("target");
    HK_CHECK(entry != NULL);
    if (entry != NULL)
        HK_CHECK(entry->d_ino == staged_inode.i_number);
    HK_CHECK(directory_buffer_writes == 1);
    HK_CHECK(directory_record_writes == 1);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(staged_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    check_namespace_released();
}

static void
failed_target_write_leaves_both_names_absent(void)
{
    reset_fixture();
    directory_buffer_write_error = EIO;
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(find_entry("stage") == NULL);
    HK_CHECK(find_entry("target") == NULL);
    HK_CHECK(directory_buffer_writes == 1);
    HK_CHECK(directory_record_writes == 1);
    HK_CHECK(staged_inode.i_nlink == 0);
    HK_CHECK(staged_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    check_namespace_released();
}

static void
existing_target_uses_production_rewrite_and_remove(void)
{
    struct direct *entry;

    reset_fixture();
    configure_stage_and_target(target_inode.i_number);
    call_publishat("stage", TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(find_entry("stage") == NULL);
    entry = find_entry("target");
    HK_CHECK(entry != NULL);
    if (entry != NULL)
        HK_CHECK(entry->d_ino == staged_inode.i_number);
    HK_CHECK(directory_buffer_writes == 0);
    HK_CHECK(directory_record_writes == 2);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(target_inode.i_nlink == 0);
    HK_CHECK(staged_inode.i_count == 1);
    HK_CHECK(target_inode.i_count == 1);
    check_namespace_released();
}

static void
restricted_paths_and_final_symlinks_precede_writes(void)
{
    static char *const invalid_paths[] = {
        "", "/stage", ".", "..", "a//stage", "a/../stage",
    };
    unsigned index;

    for (index = 0; index < sizeof(invalid_paths) / sizeof(invalid_paths[0]);
        index++) {
        reset_fixture();
        call_publishat(invalid_paths[index], -1);
        HK_CHECK(u.u_error == (index == 0 ? ENOENT : EINVAL));
        HK_CHECK(directory_buffer_writes == 0);
        HK_CHECK(directory_record_writes == 0);
        HK_CHECK(staged_inode.i_nlink == 1);
        check_namespace_released();
    }

    reset_fixture();
    configure_stage_only(symlink_inode.i_number);
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == ELOOP);
    HK_CHECK(directory_buffer_writes == 0);
    HK_CHECK(directory_record_writes == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    check_namespace_released();

    reset_fixture();
    configure_stage_and_target(symlink_inode.i_number);
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == ELOOP);
    HK_CHECK(directory_buffer_writes == 0);
    HK_CHECK(directory_record_writes == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    check_namespace_released();
}

static void
competing_renames_are_observed_after_gate_handoff(void)
{
    reset_fixture();
    HK_CHECK(ufs_namespace_enter() == 0);
    release_gate_during_wait = 1;
    replace_stage_during_wait = 1;
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(wait_calls != 0);
    HK_CHECK(directory_buffer_writes == 0);
    HK_CHECK(directory_record_writes == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(replacement_inode.i_nlink == 1);
    check_namespace_released();

    reset_fixture();
    configure_stage_and_target(target_inode.i_number);
    HK_CHECK(ufs_namespace_enter() == 0);
    release_gate_during_wait = 1;
    replace_target_during_wait = 1;
    call_publishat("stage", TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(wait_calls != 0);
    HK_CHECK(directory_buffer_writes == 0);
    HK_CHECK(directory_record_writes == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(target_inode.i_nlink == 1);
    HK_CHECK(replacement_inode.i_nlink == 1);
    check_namespace_released();
}

static void
retained_staged_reference_survives_descriptor_release(void)
{
    reset_fixture();
    release_staged_descriptor_during_lookup = 1;
    call_publishat("stage", -1);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(u.u_ofile[STAGED_DESCRIPTOR] == NULL);
    HK_CHECK(staged_inode.i_count == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(find_entry("stage") == NULL);
    HK_CHECK(find_entry("target") != NULL);
    check_namespace_released();
}

int
main(void)
{
    absent_target_uses_production_enter_and_remove();
    failed_target_write_leaves_both_names_absent();
    existing_target_uses_production_rewrite_and_remove();
    restricted_paths_and_final_symlinks_precede_writes();
    competing_renames_are_observed_after_gate_handoff();
    retained_staged_reference_survives_descriptor_release();
    return hk_verdict("publishat_beneath production namei contract");
}
