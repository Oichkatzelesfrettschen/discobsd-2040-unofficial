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

static struct inode descriptor_inode;
static int descriptor_lookups;
static int lock_calls;
static int unlock_calls;
static int access_calls;
static int access_mode;
static int access_error;

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
    (void)channel;
    (void)priority;
    (void)timeout;
    HK_CHECK(0);
    return EINTR;
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
static struct file descriptor_file;
struct inode *rootdir;
struct namecache namecache[NNAMECACHE];

struct inode *
iget(dev_t device, struct fs *filesystem, ino_t number)
{
    (void)device;
    (void)filesystem;
    (void)number;
    HK_CHECK(0);
    return NULL;
}

struct buf *
blkatoff(struct inode *inode, off_t offset, char **result)
{
    (void)inode;
    (void)offset;
    (void)result;
    HK_CHECK(0);
    return NULL;
}

struct buf *
bread(dev_t device, daddr_t block)
{
    (void)device;
    (void)block;
    HK_CHECK(0);
    return NULL;
}

daddr_t
bmap(struct inode *inode, daddr_t block, int operation, int size)
{
    (void)inode;
    (void)block;
    (void)operation;
    (void)size;
    HK_CHECK(0);
    return 0;
}

void
brelse(struct buf *buffer)
{
    (void)buffer;
    HK_CHECK(0);
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
    u = (struct user){0};
    mount[0] = (struct mount){0};
    root_inode = (struct inode){ .i_count = 1, .i_mode = IFDIR };
    cwd_inode = (struct inode){ .i_count = 1, .i_mode = IFDIR };
    descriptor_inode = (struct inode){ .i_count = 1, .i_mode = IFDIR };
    regular_inode = (struct inode){ .i_count = 1, .i_mode = IFREG };
    descriptor_file = (struct file){
        .f_count = 1,
        .f_type = DTYPE_INODE,
        .f_data = (caddr_t)&descriptor_inode,
    };
    rootdir = &root_inode;
    u.u_cdir = &cwd_inode;
    u.u_ofile[7] = &descriptor_file;
    descriptor_lookups = 0;
    lock_calls = 0;
    unlock_calls = 0;
    access_calls = 0;
    access_mode = 0;
    access_error = 0;
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

int
main(void)
{
    directory_descriptor_selects_start();
    cwd_selector_preserves_legacy_start();
    absolute_path_ignores_descriptor();
    relative_descriptor_errors_are_specific();
    copy_and_permission_failures_preserve_owners();
    return hk_verdict("directory-relative namei contract");
}

#else /* OPENAT_NAMEI_CONTRACT */

static struct file allocated_file;
static struct inode *lookup_result;
static int expected_directory_descriptor;
static caddr_t expected_path;
static int expected_lookup_operation;
static int expected_lookup_modifiers;
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

extern void kernel_open(void);
extern void kernel_openat(void);

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
    (void)parent;
    HK_CHECK(0);
    return NULL;
}

int
rdwri(enum uio_rw operation, struct inode *inode, caddr_t buffer, int length,
    off_t offset, int flags, int *residual)
{
    (void)operation;
    (void)inode;
    (void)buffer;
    (void)length;
    (void)offset;
    (void)flags;
    (void)residual;
    HK_CHECK(0);
    return EIO;
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
    (void)inode;
    (void)access_time;
    (void)modification_time;
    (void)wait;
    HK_CHECK(0);
    return EIO;
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
    (void)inode;
    (void)name;
    HK_CHECK(0);
    return EIO;
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
    if (lookup_error != 0) {
        u.u_error = lookup_error;
        return NULL;
    }
    if (lookup_result != NULL)
        lookup_result->i_flag |= ILOCKED;
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
    advisory_lock_calls++;
    return advisory_lock_error;
}

struct inode *
maknode(int mode, struct nameidata *name)
{
    (void)mode;
    (void)name;
    HK_CHECK(0);
    return NULL;
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
    HK_CHECK(inode == lookup_result);
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
    lock_calls = 0;
    unlock_calls = 0;
    access_calls = 0;
    access_mode = 0;
    access_error = 0;
}

void
iput_openat(struct inode *inode)
{
    HK_CHECK(inode == lookup_result);
    HK_CHECK((inode->i_flag & ILOCKED) != 0);
    inode->i_flag &= ~ILOCKED;
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
    kernel_openat();
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

int
main(void)
{
    relative_and_legacy_open_publish_descriptor();
    nofollow_and_directory_flags_reject_wrong_kind();
    search_open_grants_only_directory_search();
    failures_release_allocated_descriptor_and_inode();
    truncate_and_lock_failures_preserve_cleanup();
    return hk_verdict("directory-relative open contract");
}

#endif /* OPENAT_NAMEI_CONTRACT */
