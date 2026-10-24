/*
 * ILP32 contract for removing one rooted regular-file entry only while the
 * resolved inode matches an inode retained from an expected descriptor.  The
 * fixture links the production syscall, namei traversal, namespace gate, and
 * directory-removal provider.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

#define ROOT_DESCRIPTOR 3
#define EXPECTED_DESCRIPTOR 4
#define OTHER_DESCRIPTOR 5

struct user u;
struct mount mount[NMOUNT];
struct inode *rootdir;
struct namecache namecache[NNAMECACHE];

void unlinkat_beneath(void);

static struct inode root_inode;
static struct inode expected_inode;
static struct inode replacement_inode;
static struct inode directory_inode;
static struct inode first_directory_inode;
static struct inode second_directory_inode;
static struct file root_file;
static struct file expected_file;
static struct file other_file;
static struct inode *directory_target;
static char *directory_entry_name;
static struct inode *active_lookup_parent;
static union {
    struct direct entry;
    char bytes[DIRBLKSIZ];
} directory_storage;
static struct buf directory_buffer;

static int syscall_active;
static int namespace_probe;
static int release_gate_during_wait;
static int wait_error;
static int release_descriptor_reference_during_lookup;
static int directory_write_error;
static int directory_entry_removed;
static int nested_walk;
static int observed_nested_reference_peak;
static unsigned root_descriptor_lookups;
static unsigned inode_get_calls;
static unsigned directory_block_reads;
static unsigned directory_buffer_releases;
static unsigned directory_write_calls;
static unsigned wait_calls;
static unsigned handoff_waits;
static unsigned probe_waits;
static unsigned wakeup_calls;

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
    root_descriptor_lookups++;
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
    if (syscall_active && inode == &expected_inode)
        HK_CHECK(namespace_is_owned());
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
    if (nested_walk && inode == &second_directory_inode) {
        HK_CHECK(root_inode.i_count == 1);
        HK_CHECK(expected_inode.i_count == 2);
        HK_CHECK(first_directory_inode.i_count == 1);
        HK_CHECK(second_directory_inode.i_count == 1);
        observed_nested_reference_peak = 1;
    }
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
    wakeup_calls++;
}

int
tsleep(caddr_t channel, int priority, u_int timeout)
{
    HK_CHECK(channel != NULL);
    HK_CHECK(priority == (PINOD | PCATCH));
    HK_CHECK(timeout == 0);
    wait_calls++;
    if (namespace_probe) {
        probe_waits++;
        return EINTR;
    }
    if (release_gate_during_wait) {
        handoff_waits++;
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

struct inode *
iget(dev_t device, struct fs *filesystem, ino_t number)
{
    struct inode *target;

    inode_get_calls++;
    HK_CHECK(device == INODE_DEVICE(&root_inode));
    HK_CHECK(filesystem == INODE_FILESYSTEM(&root_inode));
    if (nested_walk && number == first_directory_inode.i_number)
        target = &first_directory_inode;
    else if (nested_walk && number == second_directory_inode.i_number)
        target = &second_directory_inode;
    else {
        HK_CHECK(directory_target != NULL);
        HK_CHECK(number == directory_target->i_number);
        target = directory_target;
    }
    ilock(target);
    target->i_count++;
    if (nested_walk && target == &second_directory_inode) {
        HK_CHECK(root_inode.i_count == 1);
        HK_CHECK(expected_inode.i_count == 2);
        HK_CHECK(first_directory_inode.i_count == 1);
        HK_CHECK(second_directory_inode.i_count == 1);
        observed_nested_reference_peak = 1;
    }
    return target;
}

struct buf *
bread(dev_t device, daddr_t block)
{
    struct direct *entry = &directory_storage.entry;
    struct inode *target;
    const char *entry_name;
    u_int byte_index;
    u_int length;

    directory_block_reads++;
    HK_CHECK(device == INODE_DEVICE(&root_inode));
    HK_CHECK(block == 1);
    if (nested_walk && active_lookup_parent == &root_inode) {
        target = &first_directory_inode;
        entry_name = "a";
    } else if (nested_walk &&
        active_lookup_parent == &first_directory_inode) {
        target = &second_directory_inode;
        entry_name = "b";
    } else {
        HK_CHECK(directory_target != NULL);
        HK_CHECK(directory_entry_name != NULL);
        target = directory_target;
        entry_name = directory_entry_name;
    }
    if (release_descriptor_reference_during_lookup) {
        /*
         * A caller cannot close its descriptor concurrently in this
         * single-threaded kernel.  The injected release isolates the
         * syscall's ownership of the expected inode during lookup.
         */
        release_descriptor_reference_during_lookup = 0;
        u.u_ofile[EXPECTED_DESCRIPTOR] = NULL;
        expected_file.f_count = 0;
        irele(&expected_inode);
        HK_CHECK(expected_inode.i_count == 1);
    }
    for (byte_index = 0; byte_index < sizeof(directory_storage.bytes);
        byte_index++)
        directory_storage.bytes[byte_index] = 0;
    for (length = 0; entry_name[length] != '\0'; length++)
        entry->d_name[length] = entry_name[length];
    entry->d_name[length] = '\0';
    entry->d_namlen = length;
    entry->d_ino = target->i_number;
    entry->d_reclen = DIRBLKSIZ;
    directory_buffer = (struct buf){
        .b_addr = (caddr_t)directory_storage.bytes,
        .b_bcount = DIRBLKSIZ,
    };
    return &directory_buffer;
}

daddr_t
bmap(struct inode *inode, daddr_t block, int operation, int size)
{
    if (nested_walk)
        HK_CHECK(inode == &root_inode || inode == &first_directory_inode ||
            inode == &second_directory_inode);
    else
        HK_CHECK(inode == &root_inode);
    active_lookup_parent = inode;
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
    (void)buffer;
    HK_CHECK(0);
    return EIO;
}

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t bytes, int length,
    off_t offset, int flags, int *residual)
{
    struct direct *entry = (struct direct *)bytes;

    HK_CHECK(namespace_is_owned());
    HK_CHECK(operation == UIO_WRITE);
    HK_CHECK(inode == (nested_walk ? &second_directory_inode : &root_inode));
    HK_CHECK((unsigned)length == DIRSIZ(entry));
    HK_CHECK(offset == 0);
    HK_CHECK(flags == (IO_UNIT | IO_SYNC));
    HK_CHECK(residual == NULL);
    HK_CHECK(entry->d_ino == 0);
    directory_write_calls++;
    if (directory_write_error == 0)
        directory_entry_removed = 1;
    return directory_write_error;
}

static void
set_inode_location(struct inode *inode, dev_t device, struct fs *filesystem)
{
    inode->i_dev = device;
    inode->i_fs = filesystem;
}

static void
reset_fixture(void)
{
    unsigned cache_index;

    u = (struct user){0};
    mount[0] = (struct mount){ .m_dev = 1 };
    mount[1] = (struct mount){ .m_dev = 2 };
    root_inode = (struct inode){
        .i_count = 1,
        .i_number = 10,
        .i_mode = IFDIR | 0777,
        .i_size = DIRBLKSIZ,
        .i_nlink = 2,
    };
    expected_inode = (struct inode){
        .i_count = 1,
        .i_number = 20,
        .i_mode = IFREG | 0600,
        .i_nlink = 2,
    };
    replacement_inode = (struct inode){
        .i_count = 1,
        .i_number = 21,
        .i_mode = IFREG | 0600,
        .i_nlink = 2,
    };
    directory_inode = (struct inode){
        .i_count = 1,
        .i_number = 22,
        .i_mode = IFDIR | 0700,
        .i_nlink = 2,
    };
    first_directory_inode = (struct inode){
        .i_number = 30,
        .i_id = 30,
        .i_mode = IFDIR | 0700,
        .i_size = DIRBLKSIZ,
        .i_nlink = 2,
    };
    second_directory_inode = (struct inode){
        .i_number = 31,
        .i_id = 31,
        .i_mode = IFDIR | 0700,
        .i_size = DIRBLKSIZ,
        .i_nlink = 2,
    };
    set_inode_location(&root_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&expected_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&replacement_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&directory_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&first_directory_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&second_directory_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    root_file = (struct file){
        .f_count = 1,
        .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&root_inode,
    };
    expected_file = (struct file){
        .f_count = 1,
        .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&expected_inode,
    };
    other_file = (struct file){
        .f_count = 1,
        .f_type = DTYPE_PIPE,
    };
    u.u_ofile[ROOT_DESCRIPTOR] = &root_file;
    u.u_ofile[EXPECTED_DESCRIPTOR] = &expected_file;
    u.u_ofile[OTHER_DESCRIPTOR] = &other_file;
    rootdir = &root_inode;
    directory_target = &expected_inode;
    directory_entry_name = "leaf";
    active_lookup_parent = NULL;
    for (cache_index = 0; cache_index < NNAMECACHE; cache_index++)
        namecache[cache_index] = (struct namecache){0};
    directory_buffer = (struct buf){0};
    syscall_active = 0;
    namespace_probe = 0;
    release_gate_during_wait = 0;
    wait_error = EINTR;
    release_descriptor_reference_during_lookup = 0;
    directory_write_error = 0;
    directory_entry_removed = 0;
    nested_walk = 0;
    observed_nested_reference_peak = 0;
    root_descriptor_lookups = 0;
    inode_get_calls = 0;
    directory_block_reads = 0;
    directory_buffer_releases = 0;
    directory_write_calls = 0;
    wait_calls = 0;
    handoff_waits = 0;
    probe_waits = 0;
    wakeup_calls = 0;
    nchstats = (struct nchstats){0};
}

static void
call_unlinkat_beneath(int root_descriptor, char *path,
    int expected_descriptor)
{
    struct {
        int root_descriptor;
        char *path;
        int expected_descriptor;
    } arguments = {
        .root_descriptor = root_descriptor,
        .path = path,
        .expected_descriptor = expected_descriptor,
    };

    _Static_assert(sizeof(arguments) == sizeof(int) * 3,
        "unlinkat_beneath arguments must occupy three words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    syscall_active = 1;
    unlinkat_beneath();
    syscall_active = 0;
}

static void
populate_nested_name_cache(void)
{
    namecache[0] = (struct namecache){
        .nc_ip = &first_directory_inode,
        .nc_ino = root_inode.i_number,
        .nc_dev = INODE_DEVICE(&root_inode),
        .nc_idev = INODE_DEVICE(&first_directory_inode),
        .nc_id = first_directory_inode.i_id,
        .nc_used = 1,
        .nc_nlen = 1,
        .nc_name = "a",
    };
    namecache[1] = (struct namecache){
        .nc_ip = &second_directory_inode,
        .nc_ino = first_directory_inode.i_number,
        .nc_dev = INODE_DEVICE(&first_directory_inode),
        .nc_idev = INODE_DEVICE(&second_directory_inode),
        .nc_id = second_directory_inode.i_id,
        .nc_used = 1,
        .nc_nlen = 1,
        .nc_name = "b",
    };
}

static void
expected_descriptor_validation_precedes_lookup(void)
{
    reset_fixture();
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", -1);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(root_descriptor_lookups == 0);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();

    reset_fixture();
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", OTHER_DESCRIPTOR);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(root_descriptor_lookups == 0);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();

    reset_fixture();
    expected_inode.i_mode = IFDIR | 0700;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(root_descriptor_lookups == 0);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();
}

static void
restricted_path_and_root_errors_release_ownership(void)
{
    reset_fixture();
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == ENOENT);
    HK_CHECK(root_descriptor_lookups == 0);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();

    reset_fixture();
    call_unlinkat_beneath(-1, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(root_descriptor_lookups == 1);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();

    reset_fixture();
    call_unlinkat_beneath(OTHER_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == ENOTDIR);
    HK_CHECK(root_descriptor_lookups == 1);
    HK_CHECK(expected_inode.i_count == 1);
    check_namespace_released();
}

static void
interrupted_gate_wait_preserves_the_competing_owner(void)
{
    reset_fixture();
    HK_CHECK(ufs_namespace_enter() == 0);
    wait_error = ERESTART;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == ERESTART);
    HK_CHECK(root_descriptor_lookups == 0);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(namespace_is_owned());
    ufs_namespace_exit();
    check_namespace_released();
}

static void
identity_mismatch_preserves_the_resolved_entry(void)
{
    reset_fixture();
    directory_target = &replacement_inode;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(directory_entry_removed == 0);
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(replacement_inode.i_count == 1);
    HK_CHECK(expected_inode.i_nlink == 2);
    HK_CHECK(replacement_inode.i_nlink == 2);
    HK_CHECK((root_inode.i_flag & ILOCKED) == 0);
    HK_CHECK((replacement_inode.i_flag & ILOCKED) == 0);
    check_namespace_released();

    reset_fixture();
    directory_target = &directory_inode;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(directory_inode.i_count == 1);
    HK_CHECK(directory_inode.i_nlink == 2);
    check_namespace_released();
}

static void
inode_and_parent_flags_refuse_removal(void)
{
    reset_fixture();
    expected_inode.i_flags = IMMUTABLE;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(expected_inode.i_nlink == 2);
    check_namespace_released();

    reset_fixture();
    root_inode.i_flags = APPEND;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(expected_inode.i_nlink == 2);
    check_namespace_released();

    reset_fixture();
    u.u_uid = 100;
    root_inode.i_mode |= ISVTX;
    root_inode.i_uid = 200;
    expected_inode.i_uid = 300;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(directory_write_calls == 0);
    HK_CHECK(expected_inode.i_nlink == 2);
    check_namespace_released();
}

static void
directory_write_failure_preserves_link_publication(void)
{
    reset_fixture();
    directory_write_error = EIO;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(directory_write_calls == 1);
    HK_CHECK(directory_entry_removed == 0);
    HK_CHECK(expected_inode.i_nlink == 2);
    HK_CHECK((expected_inode.i_flag & ICHG) == 0);
    HK_CHECK((root_inode.i_flag & (IUPD | ICHG)) == 0);
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    check_namespace_released();
}

static void
successful_removal_holds_serialization_through_publication(void)
{
    reset_fixture();
    HK_CHECK(ufs_namespace_enter() == 0);
    release_gate_during_wait = 1;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(handoff_waits == 1);
    HK_CHECK(directory_block_reads == 1);
    HK_CHECK(directory_buffer_releases == 1);
    HK_CHECK(inode_get_calls == 1);
    HK_CHECK(directory_write_calls == 1);
    HK_CHECK(directory_entry_removed == 1);
    HK_CHECK(expected_inode.i_nlink == 1);
    HK_CHECK((expected_inode.i_flag & ICHG) != 0);
    HK_CHECK((root_inode.i_flag & (IUPD | ICHG)) == (IUPD | ICHG));
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    HK_CHECK((expected_inode.i_flag & ILOCKED) == 0);
    HK_CHECK((root_inode.i_flag & ILOCKED) == 0);
    HK_CHECK(probe_waits != 0);
    HK_CHECK(wakeup_calls != 0);
    check_namespace_released();
}

static void
retained_expected_reference_survives_injected_descriptor_release(void)
{
    reset_fixture();
    release_descriptor_reference_during_lookup = 1;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(u.u_ofile[EXPECTED_DESCRIPTOR] == NULL);
    HK_CHECK(expected_inode.i_count == 0);
    HK_CHECK(expected_inode.i_nlink == 1);
    HK_CHECK(directory_entry_removed == 1);
    check_namespace_released();
}

static void
nested_removal_covers_cold_and_cached_ancestor_walks(void)
{
    reset_fixture();
    nested_walk = 1;
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "a/b/leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(observed_nested_reference_peak);
    HK_CHECK(inode_get_calls == 3);
    HK_CHECK(directory_block_reads == 3);
    HK_CHECK(directory_buffer_releases == 3);
    HK_CHECK(directory_entry_removed == 1);
    HK_CHECK(first_directory_inode.i_count == 0);
    HK_CHECK(second_directory_inode.i_count == 0);
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    check_namespace_released();

    reset_fixture();
    nested_walk = 1;
    populate_nested_name_cache();
    call_unlinkat_beneath(ROOT_DESCRIPTOR, "a/b/leaf", EXPECTED_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(observed_nested_reference_peak);
    HK_CHECK(nchstats.ncs_goodhits == 2);
    HK_CHECK(inode_get_calls == 1);
    HK_CHECK(directory_block_reads == 1);
    HK_CHECK(directory_buffer_releases == 1);
    HK_CHECK(directory_entry_removed == 1);
    HK_CHECK(first_directory_inode.i_count == 0);
    HK_CHECK(second_directory_inode.i_count == 0);
    HK_CHECK(expected_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    check_namespace_released();
}

int
main(void)
{
    expected_descriptor_validation_precedes_lookup();
    restricted_path_and_root_errors_release_ownership();
    interrupted_gate_wait_preserves_the_competing_owner();
    identity_mismatch_preserves_the_resolved_entry();
    inode_and_parent_flags_refuse_removal();
    directory_write_failure_preserves_link_publication();
    successful_removal_holds_serialization_through_publication();
    retained_expected_reference_survives_injected_descriptor_release();
    nested_removal_covers_cold_and_cached_ancestor_walks();
    return hk_verdict("rooted identity-conditioned unlink");
}
