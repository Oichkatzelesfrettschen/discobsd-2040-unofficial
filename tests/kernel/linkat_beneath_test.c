/*
 * ILP32 contract for publishing a rooted hard link only while the source
 * pathname identifies the regular inode retained by the expected descriptor.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/user.h>

#include <machine/limits.h>

#define ROOT_DESCRIPTOR 3
#define SOURCE_DESCRIPTOR 4
#define OTHER_DESCRIPTOR 5

struct user u;
struct timeval time;

void linkat_beneath(void);

static struct inode root_inode;
static struct inode other_parent_inode;
static struct inode source_inode;
static struct inode replacement_inode;
static struct inode directory_inode;
static struct file root_file;
static struct file source_file;
static struct file other_file;

static struct inode *source_lookup_inode;
static struct inode *target_lookup_inode;
static struct inode *target_parent;
static int source_lookup_error;
static int target_lookup_error;
static int namespace_error;
static int inode_update_error;
static int directory_write_error;
static int namespace_owned;
static int lookup_index;
static unsigned enter_calls;
static unsigned update_calls;

static void
copy_bytes(caddr_t destination, const caddr_t source, u_int length)
{
    u_int index;

    for (index = 0; index < length; index++)
        destination[index] = source[index];
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
    HK_CHECK((inode->i_flag & ILOCKED) == 0);
    HK_CHECK(inode->i_count != 0);
    inode->i_count--;
}

void
iput(struct inode *inode)
{
    HK_CHECK(inode != NULL);
    if (inode->i_flag & ILOCKED)
        iunlock(inode);
    irele(inode);
}

void
igrab(struct inode *inode)
{
    ilock(inode);
    inode->i_count++;
}

int
iupdat(struct inode *inode, struct timeval *access_time,
    struct timeval *modify_time, int waitfor)
{
    HK_CHECK(inode == &source_inode);
    HK_CHECK(access_time == &time);
    HK_CHECK(modify_time == &time);
    HK_CHECK(waitfor == 1);
    update_calls++;
    return inode_update_error;
}

int
ufs_namespace_enter(void)
{
    HK_CHECK(namespace_owned == 0);
    if (namespace_error != 0)
        return namespace_error;
    namespace_owned = 1;
    return 0;
}

void
ufs_namespace_exit(void)
{
    HK_CHECK(namespace_owned == 1);
    namespace_owned = 0;
}

static struct inode *
return_source_lookup(struct inode *inode, int error)
{
    if (error != 0) {
        u.u_error = error;
        return NULL;
    }
    inode->i_count++;
    ilock(inode);
    u.u_error = 0;
    return inode;
}

static struct inode *
return_target_lookup(struct nameidata *name_data, struct inode *inode,
    int error)
{
    if (error != 0) {
        u.u_error = error;
        return NULL;
    }
    target_parent->i_count++;
    ilock(target_parent);
    name_data->ni_pdir = target_parent;
    if (inode != NULL) {
        inode->i_count++;
        ilock(inode);
    }
    u.u_error = 0;
    return inode;
}

struct inode *
namei_at(struct nameidata *name_data, int root_descriptor)
{
    int operation = name_data->ni_nameiop & (CREATE | DELETE);

    HK_CHECK(namespace_owned == 1);
    HK_CHECK(root_descriptor == ROOT_DESCRIPTOR);
    HK_CHECK((name_data->ni_nameiop & NI_BENEATH) != 0);
    HK_CHECK((name_data->ni_nameiop & NOCACHE) != 0);
    lookup_index++;
    if (lookup_index == 1) {
        HK_CHECK(operation == LOOKUP);
        HK_CHECK((name_data->ni_nameiop & LOCKPARENT) == 0);
        HK_CHECK((name_data->ni_nameiop & FOLLOW) == 0);
        HK_CHECK(hk_streq(name_data->ni_dirp, "source"));
        return return_source_lookup(source_lookup_inode,
            source_lookup_error);
    }
    HK_CHECK(lookup_index == 2);
    HK_CHECK(operation == CREATE);
    HK_CHECK((name_data->ni_nameiop & LOCKPARENT) != 0);
    HK_CHECK((name_data->ni_nameiop & FOLLOW) == 0);
    HK_CHECK(hk_streq(name_data->ni_dirp, "target"));
    return return_target_lookup(name_data, target_lookup_inode,
        target_lookup_error);
}

int
direnter(struct inode *inode, struct nameidata *name_data)
{
    HK_CHECK(namespace_owned == 1);
    HK_CHECK(inode == &source_inode);
    HK_CHECK(name_data->ni_pdir == target_parent);
    enter_calls++;
    iput(name_data->ni_pdir);
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
    static struct fs source_filesystem;
    static struct fs other_filesystem;

    u = (struct user){0};
    source_filesystem = (struct fs){0};
    other_filesystem = (struct fs){0};
    root_inode = (struct inode){0};
    other_parent_inode = (struct inode){0};
    source_inode = (struct inode){0};
    replacement_inode = (struct inode){0};
    directory_inode = (struct inode){0};
    root_file = (struct file){0};
    source_file = (struct file){0};
    other_file = (struct file){0};

    root_inode.i_mode = IFDIR | 0777;
    root_inode.i_count = 1;
    root_inode.i_number = 2;
    other_parent_inode.i_mode = IFDIR | 0777;
    other_parent_inode.i_count = 1;
    other_parent_inode.i_number = 3;
    source_inode.i_mode = IFREG | 0600;
    source_inode.i_count = 1;
    source_inode.i_nlink = 1;
    source_inode.i_number = 10;
    replacement_inode.i_mode = IFREG | 0600;
    replacement_inode.i_count = 1;
    replacement_inode.i_nlink = 1;
    replacement_inode.i_number = 11;
    directory_inode.i_mode = IFDIR | 0700;
    directory_inode.i_count = 1;

    set_inode_location(&root_inode, 1, &source_filesystem);
    set_inode_location(&source_inode, 1, &source_filesystem);
    set_inode_location(&replacement_inode, 1, &source_filesystem);
    set_inode_location(&directory_inode, 1, &source_filesystem);
    set_inode_location(&other_parent_inode, 2, &other_filesystem);

    root_file.f_type = DTYPE_INODE;
    root_file.f_data = (caddr_t)&root_inode;
    source_file.f_type = DTYPE_INODE;
    source_file.f_data = (caddr_t)&source_inode;
    other_file.f_type = DTYPE_INODE;
    other_file.f_data = (caddr_t)&directory_inode;
    u.u_ofile[ROOT_DESCRIPTOR] = &root_file;
    u.u_ofile[SOURCE_DESCRIPTOR] = &source_file;
    u.u_ofile[OTHER_DESCRIPTOR] = &other_file;

    source_lookup_inode = &source_inode;
    target_lookup_inode = NULL;
    target_parent = &root_inode;
    source_lookup_error = 0;
    target_lookup_error = 0;
    namespace_error = 0;
    inode_update_error = 0;
    directory_write_error = 0;
    namespace_owned = 0;
    lookup_index = 0;
    enter_calls = 0;
    update_calls = 0;
}

static void
call_linkat_beneath(int root_descriptor, int source_descriptor)
{
    struct {
        int root_descriptor;
        char *source_path;
        int source_descriptor;
        char *target_path;
    } arguments = {
        .root_descriptor = root_descriptor,
        .source_path = "source",
        .source_descriptor = source_descriptor,
        .target_path = "target",
    };

    _Static_assert(sizeof(arguments) == sizeof(int) * 4,
        "linkat_beneath arguments must occupy four words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    linkat_beneath();
}

static void
absent_target_publishes_retained_source(void)
{
    reset_fixture();
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(lookup_index == 2);
    HK_CHECK(update_calls == 1);
    HK_CHECK(enter_calls == 1);
    HK_CHECK(source_inode.i_nlink == 2);
    HK_CHECK(source_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    HK_CHECK(namespace_owned == 0);
}

static void
descriptor_and_namespace_failures_precede_lookup(void)
{
    reset_fixture();
    call_linkat_beneath(ROOT_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(lookup_index == 0);

    reset_fixture();
    call_linkat_beneath(ROOT_DESCRIPTOR, OTHER_DESCRIPTOR);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(lookup_index == 0);

    reset_fixture();
    namespace_error = EINTR;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EINTR);
    HK_CHECK(source_inode.i_count == 1);
    HK_CHECK(namespace_owned == 0);
}

static void
source_identity_and_policy_failures_preserve_link_count(void)
{
    reset_fixture();
    source_lookup_inode = &replacement_inode;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    source_lookup_error = ELOOP;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == ELOOP);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    source_inode.i_flags = UF_IMMUTABLE;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    source_inode.i_flags = APPEND;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    source_inode.i_nlink = USHRT_MAX;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EMLINK);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(update_calls == 0);
    HK_CHECK(source_inode.i_nlink == USHRT_MAX);
}

static void
target_and_write_failures_roll_back_reservation(void)
{
    reset_fixture();
    inode_update_error = EIO;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    target_lookup_inode = &replacement_inode;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EEXIST);
    HK_CHECK(source_inode.i_nlink == 1);
    HK_CHECK(enter_calls == 0);

    reset_fixture();
    target_lookup_error = EACCES;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EACCES);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    target_parent = &other_parent_inode;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EXDEV);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    directory_write_error = EIO;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(enter_calls == 1);
    HK_CHECK(source_inode.i_nlink == 1);

    reset_fixture();
    root_inode.i_flags = APPEND;
    call_linkat_beneath(ROOT_DESCRIPTOR, SOURCE_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(enter_calls == 1);
    HK_CHECK(source_inode.i_nlink == 2);
}

int
main(void)
{
    absent_target_publishes_retained_source();
    descriptor_and_namespace_failures_precede_lookup();
    source_identity_and_policy_failures_preserve_link_count();
    target_and_write_failures_roll_back_reservation();
    return hk_verdict("linkat_beneath ILP32 contract");
}
