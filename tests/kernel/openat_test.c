/*
 * ILP32 contract for directory-relative pathname lookup and open. The namei
 * form links the production traversal and uses degenerate paths to observe
 * starting-directory ownership without replacing the directory walker. The
 * open form links the production syscall and vnode-open paths with a
 * controlled lookup result so final-component and cleanup effects stay
 * independently observable.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

struct user u;
struct mount mount[NMOUNT];
struct timeval time;

static struct inode descriptor_inode;
static int descriptor_lookups;
static int lock_calls;
static int unlock_calls;
static int access_calls;
static int access_mode;
static int access_error;
static int namespace_probe;
static int namespace_wait_error;

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
    lock_calls++;
    inode->i_flag |= ILOCKED;
}

void
iunlock(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    HK_CHECK((inode->i_flag & ILOCKED) != 0);
    unlock_calls++;
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

int
access(struct inode *inode, int mode)
{
    HK_CHECK(inode != NULL);
    access_calls++;
    access_mode = mode;
    if (access_error != 0)
        u.u_error = access_error;
    return access_error;
}

void
wakeup(caddr_t channel)
{
    (void)channel;
}

int
tsleep(caddr_t channel, int priority, u_int timeout)
{
    HK_CHECK(channel != NULL);
    HK_CHECK(priority == (PINOD | PCATCH));
    HK_CHECK(timeout == 0);
    if (namespace_probe)
        return EINTR;
    if (namespace_wait_error != 0)
        return namespace_wait_error;
    HK_CHECK(0);
    return EINTR;
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

void
igrab(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    ilock(inode);
    inode->i_count++;
}

#ifdef OPENAT_NAMEI_CONTRACT

static struct inode root_inode;
static struct inode cwd_inode;
static struct inode regular_inode;
static struct inode symlink_inode;
#ifndef SINGLE_UFS_ROOT
static struct inode mounted_inode;
#endif
static struct file descriptor_file;
static struct inode *directory_target;
static char *directory_entry_name;
static union {
    struct direct entry;
    char bytes[DIRBLKSIZ];
} directory_storage;
static struct buf directory_buffer;
static int inode_get_calls;
static int directory_block_reads;
static int directory_buffer_releases;
struct inode *rootdir;
struct namecache namecache[NNAMECACHE];

static void
set_inode_location(struct inode *inode, dev_t device, struct fs *filesystem)
{
#ifndef SINGLE_UFS_ROOT
    inode->i_dev = device;
    inode->i_fs = filesystem;
#else
    (void)inode;
    (void)device;
    (void)filesystem;
#endif
}

static void
configure_directory_entry(char *name, struct inode *target)
{
    directory_entry_name = name;
    directory_target = target;
}

struct inode *
iget(dev_t device, struct fs *filesystem, ino_t number)
{
    inode_get_calls++;
    HK_CHECK(directory_target != NULL);
    HK_CHECK(device == INODE_DEVICE(&descriptor_inode));
    HK_CHECK(filesystem == INODE_FILESYSTEM(&descriptor_inode));
    HK_CHECK(number == directory_target->i_number);
    ilock(directory_target);
    directory_target->i_count++;
    return directory_target;
}

struct buf *
bread(dev_t device, daddr_t block)
{
    struct direct *entry = &directory_storage.entry;
    u_int byte_index;
    u_int length;

    directory_block_reads++;
    HK_CHECK(device == INODE_DEVICE(&descriptor_inode));
    HK_CHECK(block == 1);
    HK_CHECK(directory_target != NULL);
    HK_CHECK(directory_entry_name != NULL);
    for (byte_index = 0; byte_index < sizeof(directory_storage.bytes);
        byte_index++)
        directory_storage.bytes[byte_index] = 0;
    for (length = 0; directory_entry_name[length] != '\0'; length++)
        entry->d_name[length] = directory_entry_name[length];
    entry->d_name[length] = '\0';
    entry->d_namlen = length;
    entry->d_ino = directory_target->i_number;
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
    HK_CHECK(inode == &descriptor_inode);
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

void
dirbad(struct inode *inode, off_t offset, char *message)
{
    (void)inode;
    (void)offset;
    (void)message;
    HK_CHECK(0);
}

int
dirbadentry(struct direct *entry, int offset)
{
    (void)entry;
    (void)offset;
    HK_CHECK(0);
    return 1;
}

static void
reset_namei_fixture(void)
{
    unsigned byte_index;
    unsigned cache_index;

    u = (struct user){0};
    mount[0] = (struct mount){0};
#ifndef SINGLE_UFS_ROOT
    mount[1] = (struct mount){0};
#endif
    mount[0].m_dev = 1;
#ifndef SINGLE_UFS_ROOT
    mount[1].m_dev = 2;
#endif
    root_inode = (struct inode){
        .i_count = 1, .i_number = ROOTINO, .i_mode = IFDIR,
    };
    cwd_inode = (struct inode){
        .i_count = 1, .i_number = 3, .i_mode = IFDIR,
    };
    descriptor_inode = (struct inode){
        .i_count = 1, .i_number = 10, .i_mode = IFDIR,
        .i_size = DIRBLKSIZ,
    };
    regular_inode = (struct inode){
        .i_count = 1, .i_number = 20, .i_mode = IFREG,
    };
    symlink_inode = (struct inode){
        .i_count = 1, .i_number = 21, .i_mode = IFLNK,
    };
#ifndef SINGLE_UFS_ROOT
    mounted_inode = (struct inode){
        .i_count = 1, .i_number = ROOTINO, .i_mode = IFDIR,
    };
#endif
    set_inode_location(&root_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&cwd_inode, mount[0].m_dev, &mount[0].m_filsys);
    set_inode_location(&descriptor_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&regular_inode, mount[0].m_dev,
        &mount[0].m_filsys);
    set_inode_location(&symlink_inode, mount[0].m_dev,
        &mount[0].m_filsys);
#ifndef SINGLE_UFS_ROOT
    set_inode_location(&mounted_inode, mount[1].m_dev,
        &mount[1].m_filsys);
#endif
    descriptor_file = (struct file){
        .f_count = 1,
        .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&descriptor_inode,
    };
    rootdir = &root_inode;
    u.u_cdir = &cwd_inode;
    u.u_ofile[7] = &descriptor_file;
    for (cache_index = 0; cache_index < NNAMECACHE; cache_index++)
        namecache[cache_index] = (struct namecache){0};
    directory_target = NULL;
    directory_entry_name = NULL;
    for (byte_index = 0; byte_index < sizeof(directory_storage.bytes);
        byte_index++)
        directory_storage.bytes[byte_index] = 0;
    directory_buffer = (struct buf){0};
    descriptor_lookups = 0;
    lock_calls = 0;
    unlock_calls = 0;
    access_calls = 0;
    access_mode = 0;
    access_error = 0;
    inode_get_calls = 0;
    directory_block_reads = 0;
    directory_buffer_releases = 0;
}

static struct inode *
lookup_kernel_path(int directory_descriptor, char *path)
{
    struct nameidata name;

    NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, path);
    return namei_at(&name, directory_descriptor);
}

static void
directory_descriptor_selects_start(void)
{
    struct inode *result;

    reset_namei_fixture();
    result = lookup_kernel_path(7, "");
    HK_CHECK(result == &descriptor_inode);
    HK_CHECK(descriptor_lookups == 1);
    HK_CHECK(descriptor_inode.i_count == 2);
    HK_CHECK(cwd_inode.i_count == 1);
    HK_CHECK(access_calls == 1 && access_mode == IEXEC);
    iput(result);
    HK_CHECK(descriptor_inode.i_count == 1);
}

static void
cwd_selector_preserves_legacy_start(void)
{
    struct inode *result;

    reset_namei_fixture();
    result = lookup_kernel_path(AT_FDCWD, "");
    HK_CHECK(result == &cwd_inode);
    HK_CHECK(descriptor_lookups == 0);
    iput(result);

    reset_namei_fixture();
    {
        struct nameidata name;

        NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, "");
        result = namei(&name);
    }
    HK_CHECK(result == &cwd_inode);
    HK_CHECK(descriptor_lookups == 0);
    iput(result);
}

static void
absolute_path_ignores_descriptor(void)
{
    struct inode *result;

    reset_namei_fixture();
    result = lookup_kernel_path(7, "/");
    HK_CHECK(result == &root_inode && descriptor_lookups == 0);
    iput(result);

    reset_namei_fixture();
    u.u_rdir = &descriptor_inode;
    result = lookup_kernel_path(AT_FDCWD, "/");
    HK_CHECK(result == &descriptor_inode);
    HK_CHECK(descriptor_lookups == 0);
    iput(result);
}

static void
relative_descriptor_errors_are_specific(void)
{
    struct nameidata name;

    reset_namei_fixture();
    NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, "");
    HK_CHECK(namei_at(&name, NOFILE + 4) == NULL);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(descriptor_lookups == 1);

    reset_namei_fixture();
    descriptor_file.f_type = DTYPE_SOCKET;
    NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, "");
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == ENOTDIR);
    HK_CHECK(descriptor_inode.i_count == 1);

    reset_namei_fixture();
    descriptor_file.f_data = (caddr_t)&regular_inode;
    NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, "");
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == ENOTDIR);
    HK_CHECK(regular_inode.i_count == 1);
}

static void
copy_and_permission_failures_preserve_owners(void)
{
    struct nameidata name;

    reset_namei_fixture();
    NAMEI_INIT_USER(&name, LOOKUP, FOLLOW, (caddr_t)1);
    HK_CHECK(namei_at(&name, NOFILE + 4) == NULL);
    HK_CHECK(u.u_error == EFAULT);
    HK_CHECK(descriptor_lookups == 0);
    HK_CHECK(lock_calls == 0);

    reset_namei_fixture();
    access_error = EACCES;
    NAMEI_INIT_KERNEL(&name, LOOKUP, FOLLOW, "");
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == EACCES);
    HK_CHECK(descriptor_inode.i_count == 1);
    HK_CHECK(lock_calls == 0 && unlock_calls == 1);
}

static void
restricted_lookup_rejects_escaping_syntax(void)
{
    static char *const invalid_paths[] = {
        "/absolute", ".", "..", "a//b", "a/./b", "a/../b", "a/",
    };
    struct nameidata name;
    unsigned index;

    NAMEI_INIT_KERNEL(&name, LOOKUP, NOFOLLOW, "leaf");
    NAMEI_RESTRICT_BENEATH(&name);
    NAMEI_RESET_OPERATION(&name, DELETE, LOCKPARENT);
    HK_CHECK((name.ni_nameiop & NI_BENEATH) != 0);

    reset_namei_fixture();
    NAMEI_INIT_KERNEL(&name, LOOKUP, NOFOLLOW, "");
    NAMEI_RESTRICT_BENEATH(&name);
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == ENOENT);
    HK_CHECK(descriptor_lookups == 0);

    for (index = 0; index < sizeof(invalid_paths) / sizeof(invalid_paths[0]);
        index++) {
        reset_namei_fixture();
        NAMEI_INIT_KERNEL(&name, LOOKUP, NOFOLLOW, invalid_paths[index]);
        NAMEI_RESTRICT_BENEATH(&name);
        HK_CHECK(namei_at(&name, 7) == NULL);
        HK_CHECK(u.u_error == EINVAL);
        HK_CHECK(descriptor_lookups == 0);
    }
}

static struct inode *
restricted_lookup(char *path, int operation, int modifiers)
{
    struct nameidata name = {0};

    NAMEI_INIT_KERNEL(&name, operation, modifiers, path);
    NAMEI_RESTRICT_BENEATH(&name);
    return namei_at(&name, 7);
}

static void
restricted_lookup_accepts_plain_components(void)
{
    struct inode *result;

    reset_namei_fixture();
    configure_directory_entry("leaf", &regular_inode);
    result = restricted_lookup("leaf", LOOKUP, NOFOLLOW);
    HK_CHECK(result == &regular_inode);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(descriptor_lookups == 1);
    HK_CHECK(directory_block_reads == 1);
    HK_CHECK(directory_buffer_releases == 1);
    HK_CHECK(inode_get_calls == 1);
    HK_CHECK(descriptor_inode.i_count == 1);
    HK_CHECK(regular_inode.i_count == 2);
    HK_CHECK((regular_inode.i_flag & ILOCKED) != 0);
    if (result != NULL)
        iput(result);
    HK_CHECK(regular_inode.i_count == 1);
}

static void
restricted_delete_requires_locked_parent(void)
{
    struct nameidata name = {0};

    reset_namei_fixture();
    NAMEI_INIT_KERNEL(&name, DELETE, NOFOLLOW, "leaf");
    NAMEI_RESTRICT_BENEATH(&name);
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(descriptor_lookups == 0);
    HK_CHECK(descriptor_inode.i_count == 1);
}

static void
restricted_lookup_rejects_symlinks(void)
{
    static char final_path[] = "link";
    static char intermediate_path[] = "link/leaf";
    char *paths[] = { final_path, intermediate_path };
    unsigned index;

    for (index = 0; index < sizeof(paths) / sizeof(paths[0]); index++) {
        reset_namei_fixture();
        configure_directory_entry("link", &symlink_inode);
        HK_CHECK(restricted_lookup(paths[index], LOOKUP, NOFOLLOW) == NULL);
        HK_CHECK(u.u_error == ELOOP);
        HK_CHECK(descriptor_lookups == 1);
        HK_CHECK(directory_block_reads == 1);
        HK_CHECK(directory_buffer_releases == 1);
        HK_CHECK(inode_get_calls == 1);
        HK_CHECK(descriptor_inode.i_count == 1);
        HK_CHECK(symlink_inode.i_count == 1);
        HK_CHECK((descriptor_inode.i_flag & ILOCKED) == 0);
        HK_CHECK((symlink_inode.i_flag & ILOCKED) == 0);
    }
}

#ifndef SINGLE_UFS_ROOT
static void
restricted_lookup_rejects_mount_transitions(void)
{
    reset_namei_fixture();
    configure_directory_entry("mounted", &mounted_inode);
    HK_CHECK(restricted_lookup("mounted", LOOKUP, NOFOLLOW) == NULL);
    HK_CHECK(u.u_error == EXDEV);
    HK_CHECK(descriptor_lookups == 1);
    HK_CHECK(directory_block_reads == 1);
    HK_CHECK(directory_buffer_releases == 1);
    HK_CHECK(inode_get_calls == 1);
    HK_CHECK(descriptor_inode.i_count == 1);
    HK_CHECK(mounted_inode.i_count == 1);
    HK_CHECK((descriptor_inode.i_flag & ILOCKED) == 0);
    HK_CHECK((mounted_inode.i_flag & ILOCKED) == 0);
}
#endif

static void
restricted_delete_checks_the_locked_final_inode(void)
{
    struct inode *result;
    struct nameidata name;

    reset_namei_fixture();
    configure_directory_entry("leaf", &regular_inode);
    name = (struct nameidata){0};
    NAMEI_INIT_KERNEL(&name, DELETE, LOCKPARENT, "leaf");
    NAMEI_RESTRICT_BENEATH(&name);
    result = namei_at(&name, 7);
    HK_CHECK(result == &regular_inode);
    HK_CHECK(name.ni_pdir == &descriptor_inode);
    HK_CHECK(regular_inode.i_count == 2);
    HK_CHECK(descriptor_inode.i_count == 2);
    HK_CHECK((regular_inode.i_flag & ILOCKED) != 0);
    HK_CHECK((descriptor_inode.i_flag & ILOCKED) != 0);
    if (result != NULL)
        iput(result);
    if (name.ni_pdir != NULL)
        iput(name.ni_pdir);
    HK_CHECK(regular_inode.i_count == 1);
    HK_CHECK(descriptor_inode.i_count == 1);

    reset_namei_fixture();
    configure_directory_entry("link", &symlink_inode);
    name = (struct nameidata){0};
    NAMEI_INIT_KERNEL(&name, DELETE, LOCKPARENT, "link");
    NAMEI_RESTRICT_BENEATH(&name);
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == ELOOP);
    HK_CHECK(symlink_inode.i_count == 1);
    HK_CHECK(descriptor_inode.i_count == 1);
    HK_CHECK((symlink_inode.i_flag & ILOCKED) == 0);
    HK_CHECK((descriptor_inode.i_flag & ILOCKED) == 0);

#ifndef SINGLE_UFS_ROOT
    reset_namei_fixture();
    configure_directory_entry("mounted", &mounted_inode);
    name = (struct nameidata){0};
    NAMEI_INIT_KERNEL(&name, DELETE, LOCKPARENT, "mounted");
    NAMEI_RESTRICT_BENEATH(&name);
    HK_CHECK(namei_at(&name, 7) == NULL);
    HK_CHECK(u.u_error == EXDEV);
    HK_CHECK(mounted_inode.i_count == 1);
    HK_CHECK(descriptor_inode.i_count == 1);
    HK_CHECK((mounted_inode.i_flag & ILOCKED) == 0);
    HK_CHECK((descriptor_inode.i_flag & ILOCKED) == 0);
#endif
}

int
main(void)
{
    directory_descriptor_selects_start();
    cwd_selector_preserves_legacy_start();
    absolute_path_ignores_descriptor();
    relative_descriptor_errors_are_specific();
    copy_and_permission_failures_preserve_owners();
    restricted_lookup_rejects_escaping_syntax();
    restricted_lookup_accepts_plain_components();
    restricted_delete_requires_locked_parent();
    restricted_lookup_rejects_symlinks();
#ifndef SINGLE_UFS_ROOT
    restricted_lookup_rejects_mount_transitions();
#endif
    restricted_delete_checks_the_locked_final_inode();
    check_namespace_released();
    return hk_verdict("directory-relative namei contract");
}

#else /* OPENAT_NAMEI_CONTRACT */

static struct file allocated_file;
static struct inode *lookup_result;
static int expected_directory_descriptor;
static caddr_t expected_path;
static int expected_lookup_operation;
static int expected_lookup_modifiers;
static int expected_namei_policy;
static int lookup_calls;
static int lookup_error;
static int allocation_calls;
static int allocation_error;
static int close_calls;
static int inode_put_calls;
static int truncate_calls;
static int open_inode_calls;
static int open_inode_mode;
static int open_inode_error;
static int jump_result;
static int advisory_lock_calls;
static int advisory_lock_error;
static struct inode mkdir_parent_inode;
static struct inode mkdir_created_inode;
static int mkdir_active;
static int regular_create_active;
static int inode_allocation_calls;
static int inode_allocation_error;
static int inode_update_calls;
static int directory_initialization_calls;
static int directory_initialization_error;
static int directory_entry_calls;
static int directory_entry_error;
static int regular_creation_calls;
static int regular_creation_error;

extern void kernel_open(void);
extern void kernel_openat(void);
extern void kernel_openat_beneath(void);
extern void kernel_mkdirat_beneath(void);
void iput_openat(struct inode *inode);

int
baduaddr(caddr_t address)
{
    (void)address;
    HK_CHECK(0);
    return 1;
}

int
copyout(const caddr_t source, caddr_t destination, u_int length)
{
    (void)source;
    (void)destination;
    (void)length;
    HK_CHECK(0);
    return EFAULT;
}

int
groupmember(gid_t group)
{
    (void)group;
    HK_CHECK(0);
    return 0;
}

int
suser(void)
{
    HK_CHECK(0);
    return 0;
}

struct inode *
ialloc(struct inode *parent)
{
    HK_CHECK(mkdir_active);
    HK_CHECK(parent == &mkdir_parent_inode);
    HK_CHECK(namespace_is_owned());
    inode_allocation_calls++;
    if (inode_allocation_error != 0) {
        u.u_error = inode_allocation_error;
        return NULL;
    }
    mkdir_created_inode.i_flag |= ILOCKED;
    return &mkdir_created_inode;
}

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t buffer, int length,
    off_t offset, int flags, int *residual)
{
    const struct dirtemplate *template = (const struct dirtemplate *)buffer;

    HK_CHECK(mkdir_active);
    HK_CHECK(namespace_is_owned());
    HK_CHECK(operation == UIO_WRITE);
    HK_CHECK(inode == &mkdir_created_inode);
    HK_CHECK(length == (int)sizeof(*template));
    HK_CHECK(offset == 0);
    HK_CHECK(flags == (IO_UNIT | IO_SYNC));
    (void)residual;
    HK_CHECK(template->dot_ino == mkdir_created_inode.i_number);
    HK_CHECK(template->dotdot_ino == mkdir_parent_inode.i_number);
    directory_initialization_calls++;
    return directory_initialization_error;
}

int
ino_stat(struct inode *inode, struct stat *status)
{
    (void)inode;
    (void)status;
    HK_CHECK(0);
    return EIO;
}

int
iupdat(struct inode *inode, struct timeval *access_time,
    struct timeval *modification_time, int wait)
{
    HK_CHECK(mkdir_active);
    HK_CHECK(namespace_is_owned());
    HK_CHECK(inode == &mkdir_created_inode || inode == &mkdir_parent_inode);
    HK_CHECK(access_time == &time);
    HK_CHECK(modification_time == &time);
    HK_CHECK(wait == 1);
    inode_update_calls++;
    return 0;
}

int
checkpath(struct inode *source, struct inode *target)
{
    (void)source;
    (void)target;
    HK_CHECK(0);
    return EIO;
}

int
dirempty(struct inode *inode, ino_t parent_number)
{
    (void)inode;
    (void)parent_number;
    HK_CHECK(0);
    return 0;
}

void
dirrewrite(struct inode *directory, struct inode *inode,
    struct nameidata *name)
{
    (void)directory;
    (void)inode;
    (void)name;
    HK_CHECK(0);
}

void
cacheinval(struct inode *inode)
{
    (void)inode;
    HK_CHECK(0);
}

int
ufs_setattr(struct inode *inode, struct vattr *attributes)
{
    (void)inode;
    (void)attributes;
    HK_CHECK(0);
    return EIO;
}

int
direnter(struct inode *inode, struct nameidata *name)
{
    HK_CHECK(mkdir_active);
    HK_CHECK(namespace_is_owned());
    HK_CHECK(inode == &mkdir_created_inode);
    HK_CHECK(name->ni_pdir == &mkdir_parent_inode);
    directory_entry_calls++;
    iput_openat(&mkdir_parent_inode);
    return directory_entry_error;
}

int
dirremove(struct nameidata *name)
{
    (void)name;
    HK_CHECK(0);
    return EIO;
}

struct inode *
namei_at(struct nameidata *name, int directory_descriptor)
{
    int operation;
    int modifiers;

    lookup_calls++;
    operation = name->ni_nameiop & (CREATE | DELETE);
    modifiers = name->ni_nameiop & (LOCKPARENT | NOCACHE | FOLLOW);
    HK_CHECK(directory_descriptor == expected_directory_descriptor);
    HK_CHECK(name->ni_dirp == expected_path);
    HK_CHECK(operation == expected_lookup_operation);
    HK_CHECK(modifiers == expected_lookup_modifiers);
    HK_CHECK((name->ni_nameiop & (NI_NOEMPTY | NI_BENEATH)) ==
        expected_namei_policy);
    if (expected_namei_policy & NI_BENEATH)
        HK_CHECK(namespace_is_owned());
    if (lookup_error != 0) {
        u.u_error = lookup_error;
        return NULL;
    }
    if (lookup_result != NULL)
        lookup_result->i_flag |= ILOCKED;
    if ((mkdir_active || regular_create_active) && lookup_result == NULL) {
        mkdir_parent_inode.i_flag |= ILOCKED;
        name->ni_pdir = &mkdir_parent_inode;
    }
    name->ni_ip = lookup_result;
    return lookup_result;
}

struct inode *
namei(struct nameidata *name)
{
    return namei_at(name, AT_FDCWD);
}

struct file *
falloc(void)
{
    allocation_calls++;
    if (allocation_error != 0) {
        u.u_error = allocation_error;
        return NULL;
    }
    allocated_file = (struct file){ .f_count = 1 };
    u.u_rval = 3;
    u.u_ofile[3] = &allocated_file;
    return &allocated_file;
}

int
closef(struct file *file)
{
    HK_CHECK(file == &allocated_file);
    close_calls++;
    file->f_count = 0;
    return 0;
}

int
dupfdopen(int allocated, int source, int mode, int error)
{
    (void)allocated;
    (void)source;
    (void)mode;
    return error;
}

int
ino_lock(struct file *file, int operation)
{
    HK_CHECK(file == &allocated_file);
    HK_CHECK(operation != 0);
    if (expected_namei_policy & NI_BENEATH)
        check_namespace_released();
    advisory_lock_calls++;
    return advisory_lock_error;
}

struct inode *
maknode(int mode, struct nameidata *name)
{
    HK_CHECK(regular_create_active);
    HK_CHECK(namespace_is_owned());
    HK_CHECK(name->ni_pdir == &mkdir_parent_inode);
    regular_creation_calls++;
    if (regular_creation_error != 0) {
        u.u_error = regular_creation_error;
        iput_openat(&mkdir_parent_inode);
        return NULL;
    }
    descriptor_inode.i_mode = IFREG | (mode & 0777);
    descriptor_inode.i_flag |= ILOCKED;
    name->ni_ip = &descriptor_inode;
    iput_openat(&mkdir_parent_inode);
    return &descriptor_inode;
}

void
itrunc(struct inode *inode, off_t length, int flags)
{
    HK_CHECK(inode == lookup_result);
    HK_CHECK(length == 0);
    (void)flags;
    truncate_calls++;
}

int
openi(struct inode *inode, int mode)
{
    HK_CHECK(inode == lookup_result ||
        (regular_create_active && inode == &descriptor_inode));
    if (expected_namei_policy & NI_BENEATH)
        check_namespace_released();
    open_inode_calls++;
    open_inode_mode = mode;
    return open_inode_error;
}

int
openat_setjmp(label_t *state)
{
    (void)state;
    return jump_result;
}

static void
reset_open_fixture(void)
{
    u = (struct user){0};
    descriptor_inode = (struct inode){ .i_count = 1, .i_mode = IFREG };
    allocated_file = (struct file){0};
    lookup_result = &descriptor_inode;
    expected_directory_descriptor = AT_FDCWD;
    expected_path = NULL;
    expected_lookup_operation = LOOKUP;
    expected_lookup_modifiers = FOLLOW;
    expected_namei_policy = 0;
    lookup_calls = 0;
    lookup_error = 0;
    allocation_calls = 0;
    allocation_error = 0;
    close_calls = 0;
    inode_put_calls = 0;
    truncate_calls = 0;
    open_inode_calls = 0;
    open_inode_mode = 0;
    open_inode_error = 0;
    jump_result = 0;
    advisory_lock_calls = 0;
    advisory_lock_error = 0;
    mkdir_parent_inode = (struct inode){
        .i_count = 1,
        .i_mode = IFDIR | 0755,
        .i_nlink = 2,
        .i_gid = 44,
        .i_number = 7,
    };
    mkdir_created_inode = (struct inode){
        .i_count = 1,
        .i_number = 42,
    };
    mkdir_active = 0;
    regular_create_active = 0;
    inode_allocation_calls = 0;
    inode_allocation_error = 0;
    inode_update_calls = 0;
    directory_initialization_calls = 0;
    directory_initialization_error = 0;
    directory_entry_calls = 0;
    directory_entry_error = 0;
    regular_creation_calls = 0;
    regular_creation_error = 0;
    lock_calls = 0;
    unlock_calls = 0;
    access_calls = 0;
    access_mode = 0;
    access_error = 0;
    namespace_probe = 0;
    namespace_wait_error = 0;
}

void
iput_openat(struct inode *inode)
{
    HK_CHECK(inode == lookup_result ||
        (regular_create_active && inode == &mkdir_parent_inode) ||
        (mkdir_active && (inode == &mkdir_parent_inode ||
        inode == &mkdir_created_inode)));
    HK_CHECK((inode->i_flag & ILOCKED) != 0);
    inode->i_flag &= ~ILOCKED;
    if (mkdir_active || regular_create_active)
        inode->i_count--;
    inode_put_calls++;
}

static void
call_openat(int descriptor, char *path, int flags, int mode)
{
    struct {
        int fd;
        char *path;
        int flags;
        int mode;
    } arguments = { descriptor, path, flags, mode };

    _Static_assert(sizeof(arguments) == sizeof(int) * 4,
        "openat syscall arguments must occupy four words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    expected_directory_descriptor = descriptor;
    expected_path = path;
    expected_namei_policy = NI_NOEMPTY;
    kernel_openat();
}

static void
call_openat_beneath(int descriptor, char *path, int flags, int mode)
{
    struct {
        int rootfd;
        char *path;
        int flags;
        int mode;
    } arguments = { descriptor, path, flags, mode };

    _Static_assert(sizeof(arguments) == sizeof(int) * 4,
        "openat_beneath syscall arguments must occupy four words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    expected_directory_descriptor = descriptor;
    expected_path = path;
    expected_namei_policy = NI_NOEMPTY | NI_BENEATH;
    if (flags & O_CREAT) {
        expected_lookup_operation = CREATE;
        expected_lookup_modifiers =
            (flags & (O_EXCL | O_NOFOLLOW)) ? NOFOLLOW : FOLLOW;
        regular_create_active = 1;
    }
    kernel_openat_beneath();
    regular_create_active = 0;
}

static void
call_mkdirat_beneath(int descriptor, char *path, int mode)
{
    struct {
        int rootfd;
        char *path;
        int mode;
    } arguments = { descriptor, path, mode };

    _Static_assert(sizeof(arguments) == sizeof(int) * 3,
        "mkdirat_beneath syscall arguments must occupy three words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    expected_directory_descriptor = descriptor;
    expected_path = path;
    expected_lookup_operation = CREATE;
    expected_lookup_modifiers = NOFOLLOW;
    expected_namei_policy = NI_NOEMPTY | NI_BENEATH;
    mkdir_active = 1;
    kernel_mkdirat_beneath();
    mkdir_active = 0;
}

static void
call_open(char *path, int flags, int mode)
{
    struct {
        char *path;
        int flags;
        int mode;
    } arguments = { path, flags, mode };

    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    expected_directory_descriptor = AT_FDCWD;
    expected_path = path;
    kernel_open();
}

static void
relative_and_legacy_open_publish_descriptor(void)
{
    char relative[] = "leaf";

    reset_open_fixture();
    call_openat(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == 0 && u.u_rval == 3);
    HK_CHECK(allocation_calls == 1 && lookup_calls == 1);
    HK_CHECK(allocated_file.f_data == (caddr_t)&descriptor_inode);
    HK_CHECK(allocated_file.f_flag == FREAD);
    HK_CHECK(open_inode_calls == 1 && open_inode_mode == FREAD);

    reset_open_fixture();
    call_open(relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(expected_directory_descriptor == AT_FDCWD);
    HK_CHECK(lookup_calls == 1 && open_inode_calls == 1);
}

static void
nofollow_and_directory_flags_reject_wrong_kind(void)
{
    char relative[] = "leaf";

    reset_open_fixture();
    descriptor_inode.i_mode = IFLNK;
    expected_lookup_modifiers = NOFOLLOW;
    call_openat(7, relative, O_RDONLY | O_NOFOLLOW, 0);
    HK_CHECK(u.u_error == ELOOP);
    HK_CHECK(open_inode_calls == 0 && truncate_calls == 0);
    HK_CHECK(inode_put_calls == 1);
    HK_CHECK(u.u_ofile[3] == NULL);

    reset_open_fixture();
    call_openat(7, relative, O_RDONLY | O_DIRECTORY, 0);
    HK_CHECK(u.u_error == ENOTDIR);
    HK_CHECK(open_inode_calls == 0 && inode_put_calls == 1);
}

static void
search_open_grants_only_directory_search(void)
{
    char relative[] = "directory";

    reset_open_fixture();
    descriptor_inode.i_mode = IFDIR;
    call_openat(7, relative, O_SEARCH | O_DIRECTORY, 0);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(access_calls == 1 && access_mode == IEXEC);
    HK_CHECK((allocated_file.f_flag & FSEARCH) != 0);
    HK_CHECK((allocated_file.f_flag & (FREAD | FWRITE)) == 0);
    HK_CHECK(open_inode_mode == FSEARCH);

    reset_open_fixture();
    call_openat(7, relative, O_SEARCH | O_WRONLY, 0);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(allocation_calls == 0 && lookup_calls == 0);

    reset_open_fixture();
    call_openat(7, relative, O_SEARCH | O_CREAT, 0700);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(allocation_calls == 0 && lookup_calls == 0);
}

static void
identity_open_retains_regular_inode_without_data_access(void)
{
    static const int contradictory_flags[] = {
        O_WRONLY,
        O_RDWR,
        O_NONBLOCK,
        O_APPEND,
        O_SHLOCK,
        O_EXLOCK,
        O_ASYNC,
        O_FSYNC,
        O_CREAT,
        O_TRUNC,
        O_EXCL,
        O_NOCTTY,
        O_DIRECTORY,
        O_SEARCH
    };
    char relative[] = "regular";
    size_t flag_index;

    reset_open_fixture();
    access_error = EACCES;
    expected_lookup_modifiers = NOFOLLOW;
    call_openat_beneath(7, relative, O_IDENTITY | O_NOFOLLOW, 0);
    HK_CHECK(u.u_error == 0 && u.u_rval == 3);
    HK_CHECK(access_calls == 0 && open_inode_calls == 0);
    HK_CHECK((allocated_file.f_flag & FIDENTITY) != 0);
    HK_CHECK((allocated_file.f_flag & (FREAD | FWRITE)) == 0);
    HK_CHECK(allocated_file.f_data == (caddr_t)&descriptor_inode);
    check_namespace_released();

    reset_open_fixture();
    descriptor_inode.i_mode = IFDIR;
    expected_lookup_modifiers = NOFOLLOW;
    call_openat_beneath(7, relative, O_IDENTITY | O_NOFOLLOW, 0);
    HK_CHECK(u.u_error == EFTYPE);
    HK_CHECK(open_inode_calls == 0 && inode_put_calls == 1);

    for (flag_index = 0;
        flag_index < sizeof(contradictory_flags) /
        sizeof(contradictory_flags[0]); flag_index++) {
        reset_open_fixture();
        call_openat_beneath(7, relative,
            O_IDENTITY | contradictory_flags[flag_index], 0600);
        HK_CHECK(u.u_error == EINVAL);
        HK_CHECK(allocation_calls == 0 && lookup_calls == 0);
    }
}

static void
failures_release_allocated_descriptor_and_inode(void)
{
    char relative[] = "leaf";

    reset_open_fixture();
    allocation_error = EMFILE;
    call_openat(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == EMFILE);
    HK_CHECK(lookup_calls == 0);

    reset_open_fixture();
    lookup_error = ENOENT;
    call_openat(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == ENOENT);
    HK_CHECK(allocated_file.f_count == 0);
    HK_CHECK(u.u_ofile[3] == NULL);

    reset_open_fixture();
    open_inode_error = EIO;
    call_openat(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(inode_put_calls == 1);
    HK_CHECK(u.u_ofile[3] == NULL);

    reset_open_fixture();
    jump_result = 1;
    call_openat(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == EINTR);
    HK_CHECK(open_inode_calls == 0 && inode_put_calls == 1);
    HK_CHECK(u.u_ofile[3] == NULL);
}

static void
truncate_and_lock_failures_preserve_cleanup(void)
{
    char relative[] = "leaf";

    reset_open_fixture();
    call_openat(7, relative, O_WRONLY | O_TRUNC, 0);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(truncate_calls == 1 && open_inode_calls == 1);

    reset_open_fixture();
    advisory_lock_error = EWOULDBLOCK;
    call_openat(7, relative, O_RDONLY | O_EXLOCK, 0);
    HK_CHECK(u.u_error == EWOULDBLOCK);
    HK_CHECK(advisory_lock_calls == 1 && close_calls == 1);
    HK_CHECK(u.u_ofile[3] == NULL);
}

static void
rooted_open_holds_and_releases_namespace_serialization(void)
{
    char relative[] = "leaf";

    reset_open_fixture();
    call_openat_beneath(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == 0 && u.u_rval == 3);
    HK_CHECK(lookup_calls == 1 && open_inode_calls == 1);
    check_namespace_released();

    reset_open_fixture();
    HK_CHECK(ufs_namespace_enter() == 0);
    namespace_wait_error = ERESTART;
    call_openat_beneath(7, relative, O_RDONLY, 0);
    HK_CHECK(u.u_error == ERESTART);
    HK_CHECK(allocation_calls == 0 && lookup_calls == 0);
    HK_CHECK(namespace_is_owned());
    ufs_namespace_exit();
    check_namespace_released();
}

static void
rooted_exclusive_create_owns_the_new_inode_until_open(void)
{
    char relative[] = "directory/stage";

    reset_open_fixture();
    lookup_result = NULL;
    call_openat_beneath(7, relative,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    HK_CHECK(u.u_error == 0 && u.u_rval == 3);
    HK_CHECK(lookup_calls == 1);
    HK_CHECK(regular_creation_calls == 1);
    HK_CHECK(allocated_file.f_data == (caddr_t)&descriptor_inode);
    HK_CHECK((descriptor_inode.i_mode & (IFMT | 0777)) == (IFREG | 0600));
    HK_CHECK(open_inode_calls == 1);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    regular_creation_error = ENOSPC;
    call_openat_beneath(7, relative,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    HK_CHECK(u.u_error == ENOSPC);
    HK_CHECK(regular_creation_calls == 1);
    HK_CHECK(open_inode_calls == 0);
    HK_CHECK(allocated_file.f_count == 0);
    HK_CHECK(u.u_ofile[3] == NULL);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    check_namespace_released();
}

static void
rooted_mkdir_initializes_and_publishes_under_serialization(void)
{
    char relative[] = "directory";

    reset_open_fixture();
    u.u_uid = 33;
    u.u_cmask = 0022;
    lookup_result = NULL;
    call_mkdirat_beneath(7, relative, 0777);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(lookup_calls == 1);
    HK_CHECK(inode_allocation_calls == 1);
    HK_CHECK(inode_update_calls == 2);
    HK_CHECK(directory_initialization_calls == 1);
    HK_CHECK(directory_entry_calls == 1);
    HK_CHECK(mkdir_created_inode.i_mode == (IFDIR | 0755));
    HK_CHECK(mkdir_created_inode.i_nlink == 2);
    HK_CHECK(mkdir_created_inode.i_uid == 33);
    HK_CHECK(mkdir_created_inode.i_gid == 44);
    HK_CHECK(mkdir_created_inode.i_size == DIRBLKSIZ);
    HK_CHECK(mkdir_parent_inode.i_nlink == 3);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    HK_CHECK(mkdir_created_inode.i_count == 0);
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    directory_entry_error = EIO;
    call_mkdirat_beneath(7, relative, 0755);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(directory_initialization_calls == 1);
    HK_CHECK(directory_entry_calls == 1);
    HK_CHECK(mkdir_parent_inode.i_nlink == 2);
    HK_CHECK(mkdir_created_inode.i_nlink == 0);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    HK_CHECK(mkdir_created_inode.i_count == 0);
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    directory_entry_error = ENOSPC;
    call_mkdirat_beneath(7, relative, 0755);
    HK_CHECK(u.u_error == ENOSPC);
    HK_CHECK(directory_initialization_calls == 1);
    HK_CHECK(directory_entry_calls == 1);
    HK_CHECK(mkdir_parent_inode.i_nlink == 2);
    HK_CHECK(mkdir_created_inode.i_nlink == 0);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    HK_CHECK(mkdir_created_inode.i_count == 0);
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    HK_CHECK(ufs_namespace_enter() == 0);
    namespace_wait_error = ERESTART;
    call_mkdirat_beneath(7, relative, 0755);
    HK_CHECK(u.u_error == ERESTART);
    HK_CHECK(lookup_calls == 0 && inode_allocation_calls == 0);
    HK_CHECK(namespace_is_owned());
    ufs_namespace_exit();
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    inode_allocation_error = ENOSPC;
    call_mkdirat_beneath(7, relative, 0755);
    HK_CHECK(u.u_error == ENOSPC);
    HK_CHECK(inode_allocation_calls == 1);
    HK_CHECK(directory_initialization_calls == 0);
    HK_CHECK(directory_entry_calls == 0);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    check_namespace_released();

    reset_open_fixture();
    lookup_result = NULL;
    directory_initialization_error = EIO;
    call_mkdirat_beneath(7, relative, 0755);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(inode_allocation_calls == 1);
    HK_CHECK(directory_initialization_calls == 1);
    HK_CHECK(directory_entry_calls == 0);
    HK_CHECK(mkdir_parent_inode.i_nlink == 2);
    HK_CHECK(mkdir_created_inode.i_nlink == 0);
    HK_CHECK(mkdir_parent_inode.i_count == 0);
    HK_CHECK(mkdir_created_inode.i_count == 0);
    check_namespace_released();
}

int
main(void)
{
    relative_and_legacy_open_publish_descriptor();
    nofollow_and_directory_flags_reject_wrong_kind();
    search_open_grants_only_directory_search();
    identity_open_retains_regular_inode_without_data_access();
    failures_release_allocated_descriptor_and_inode();
    truncate_and_lock_failures_preserve_cleanup();
    rooted_open_holds_and_releases_namespace_serialization();
    rooted_exclusive_create_owns_the_new_inode_until_open();
    rooted_mkdir_initializes_and_publishes_under_serialization();
    return hk_verdict("directory-relative open contract");
}

#endif /* OPENAT_NAMEI_CONTRACT */
