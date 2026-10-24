/*
 * ILP32 contract for publishing one descriptor-owned staged regular file
 * only while the rooted source and destination retain their authorized
 * identities.
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

#define ROOT_DESCRIPTOR 3
#define STAGED_DESCRIPTOR 4
#define TARGET_DESCRIPTOR 5
#define OTHER_DESCRIPTOR 6

struct user u;
struct timeval time;

void publishat_beneath(void);

static struct inode root_inode;
static struct inode other_parent_inode;
static struct inode staged_inode;
static struct inode target_inode;
static struct inode replacement_inode;
static struct inode directory_inode;
static struct file root_file;
static struct file staged_file;
static struct file target_file;
static struct file other_file;

static struct inode *source_lookup_inode;
static struct inode *destination_lookup_inode;
static struct inode *cleanup_lookup_inode;
static struct inode *destination_parent;
static int source_lookup_error;
static int destination_lookup_error;
static int cleanup_lookup_error;
static int namespace_error;
static int inode_update_error;
static int directory_write_error;
static int directory_remove_error;
static int namespace_owned;
static int lookup_index;
static unsigned enter_calls;
static unsigned rewrite_calls;
static unsigned remove_calls;
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
    HK_CHECK(inode == &staged_inode);
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
return_lookup(struct nameidata *name_data, struct inode *parent,
    struct inode *inode, int error)
{
    if (error != 0) {
        u.u_error = error;
        return NULL;
    }
    parent->i_count++;
    ilock(parent);
    name_data->ni_pdir = parent;
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
    HK_CHECK((name_data->ni_nameiop & LOCKPARENT) != 0);
    HK_CHECK((name_data->ni_nameiop & NOCACHE) != 0);
    lookup_index++;
    if (lookup_index == 1) {
        HK_CHECK(operation == DELETE);
        HK_CHECK(hk_streq(name_data->ni_dirp, "stage"));
        return return_lookup(name_data, &root_inode, source_lookup_inode,
            source_lookup_error);
    }
    if (lookup_index == 2) {
        HK_CHECK(operation == CREATE);
        HK_CHECK(hk_streq(name_data->ni_dirp, "target"));
        return return_lookup(name_data, destination_parent,
            destination_lookup_inode, destination_lookup_error);
    }
    if (lookup_index == 3) {
        HK_CHECK(operation == DELETE);
        HK_CHECK(hk_streq(name_data->ni_dirp, "stage"));
        return return_lookup(name_data, &root_inode, cleanup_lookup_inode,
            cleanup_lookup_error);
    }
    HK_CHECK(lookup_index == 4);
    HK_CHECK(operation == CREATE);
    HK_CHECK(hk_streq(name_data->ni_dirp, "target"));
    return return_lookup(name_data, destination_parent,
        destination_lookup_inode, destination_lookup_error);
}

int
direnter(struct inode *inode, struct nameidata *name_data)
{
    HK_CHECK(namespace_owned == 1);
    HK_CHECK(inode == &staged_inode);
    HK_CHECK(name_data->ni_pdir == &root_inode);
    enter_calls++;
    iput(name_data->ni_pdir);
    return directory_write_error;
}

void
dirrewrite(struct inode *parent, struct inode *inode,
    struct nameidata *name_data)
{
    HK_CHECK(namespace_owned == 1);
    HK_CHECK(parent == &root_inode);
    HK_CHECK(inode == &staged_inode);
    HK_CHECK(name_data->ni_pdir == parent);
    rewrite_calls++;
    u.u_error = directory_write_error;
    iput(parent);
}

int
dirremove(struct nameidata *name_data)
{
    HK_CHECK(namespace_owned == 1);
    HK_CHECK(name_data->ni_pdir == &root_inode);
    remove_calls++;
    if (directory_remove_error != 0) {
        u.u_error = directory_remove_error;
        return 0;
    }
    return 1;
}

static void
reset_fixture(void)
{
    u = (struct user){0};
    root_inode = (struct inode){0};
    other_parent_inode = (struct inode){0};
    staged_inode = (struct inode){0};
    target_inode = (struct inode){0};
    replacement_inode = (struct inode){0};
    directory_inode = (struct inode){0};
    root_file = (struct file){0};
    staged_file = (struct file){0};
    target_file = (struct file){0};
    other_file = (struct file){0};

    root_inode.i_mode = IFDIR | 0777;
    root_inode.i_count = 1;
    root_inode.i_number = 2;
    root_inode.i_uid = 20;
    other_parent_inode.i_mode = IFDIR | 0777;
    other_parent_inode.i_count = 1;
    other_parent_inode.i_number = 3;
    staged_inode.i_mode = IFREG | 0600;
    staged_inode.i_count = 1;
    staged_inode.i_nlink = 1;
    staged_inode.i_number = 10;
    staged_inode.i_uid = 20;
    target_inode.i_mode = IFREG | 0600;
    target_inode.i_count = 1;
    target_inode.i_nlink = 1;
    target_inode.i_number = 11;
    target_inode.i_uid = 20;
    replacement_inode.i_mode = IFREG | 0600;
    replacement_inode.i_count = 1;
    replacement_inode.i_nlink = 1;
    replacement_inode.i_number = 12;
    replacement_inode.i_uid = 20;
    directory_inode.i_mode = IFDIR | 0700;
    directory_inode.i_count = 1;

    root_file.f_type = DTYPE_INODE;
    root_file.f_data = (caddr_t)&root_inode;
    staged_file.f_type = DTYPE_INODE;
    staged_file.f_data = (caddr_t)&staged_inode;
    target_file.f_type = DTYPE_INODE;
    target_file.f_data = (caddr_t)&target_inode;
    other_file.f_type = DTYPE_INODE;
    other_file.f_data = (caddr_t)&directory_inode;
    u.u_ofile[ROOT_DESCRIPTOR] = &root_file;
    u.u_ofile[STAGED_DESCRIPTOR] = &staged_file;
    u.u_ofile[TARGET_DESCRIPTOR] = &target_file;
    u.u_ofile[OTHER_DESCRIPTOR] = &other_file;
    u.u_uid = 20;

    source_lookup_inode = &staged_inode;
    destination_lookup_inode = NULL;
    cleanup_lookup_inode = &staged_inode;
    destination_parent = &root_inode;
    source_lookup_error = 0;
    destination_lookup_error = 0;
    cleanup_lookup_error = 0;
    namespace_error = 0;
    inode_update_error = 0;
    directory_write_error = 0;
    directory_remove_error = 0;
    namespace_owned = 0;
    lookup_index = 0;
    enter_calls = 0;
    rewrite_calls = 0;
    remove_calls = 0;
    update_calls = 0;
}

static void
call_publishat(int staged_descriptor, int target_descriptor)
{
    struct {
        int root_descriptor;
        char *staged_path;
        int staged_descriptor;
        char *target_path;
        int target_descriptor;
    } arguments = {
        .root_descriptor = ROOT_DESCRIPTOR,
        .staged_path = "stage",
        .staged_descriptor = staged_descriptor,
        .target_path = "target",
        .target_descriptor = target_descriptor,
    };

    _Static_assert(sizeof(arguments) == sizeof(int) * 5,
        "publishat_beneath arguments must occupy five words");
    copy_bytes((caddr_t)u.u_arg, (caddr_t)&arguments, sizeof(arguments));
    publishat_beneath();
}

static void
absent_destination_publishes_and_removes_stage(void)
{
    reset_fixture();
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(enter_calls == 1);
    HK_CHECK(rewrite_calls == 0);
    HK_CHECK(remove_calls == 1);
    HK_CHECK(update_calls == 1);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(staged_inode.i_count == 1);
    HK_CHECK(root_inode.i_count == 1);
    HK_CHECK(namespace_owned == 0);
}

static void
expected_destination_is_replaced(void)
{
    reset_fixture();
    destination_lookup_inode = &target_inode;
    call_publishat(STAGED_DESCRIPTOR, TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == 0);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(rewrite_calls == 1);
    HK_CHECK(remove_calls == 1);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(target_inode.i_nlink == 0);
    HK_CHECK(target_inode.i_count == 1);
}

static void
identity_mismatches_preserve_entries(void)
{
    reset_fixture();
    source_lookup_inode = &replacement_inode;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(staged_inode.i_nlink == 1);

    reset_fixture();
    destination_lookup_inode = &replacement_inode;
    call_publishat(STAGED_DESCRIPTOR, TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(rewrite_calls == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(replacement_inode.i_nlink == 1);

    reset_fixture();
    destination_lookup_inode = &target_inode;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EEXIST);
    HK_CHECK(staged_inode.i_nlink == 1);

    reset_fixture();
    call_publishat(STAGED_DESCRIPTOR, TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(staged_inode.i_nlink == 1);
}

static void
parent_and_policy_failures_precede_publication(void)
{
    reset_fixture();
    destination_parent = &other_parent_inode;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EXDEV);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(staged_inode.i_nlink == 1);

    reset_fixture();
    staged_inode.i_flags = UF_IMMUTABLE;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(lookup_index == 1);

    reset_fixture();
    root_inode.i_mode |= ISVTX;
    root_inode.i_uid = 30;
    staged_inode.i_uid = 31;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(enter_calls == 0);

    reset_fixture();
    destination_lookup_inode = &target_inode;
    target_inode.i_flags = UF_APPEND;
    call_publishat(STAGED_DESCRIPTOR, TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(rewrite_calls == 0);

    reset_fixture();
    destination_lookup_inode = &target_inode;
    root_inode.i_mode |= ISVTX;
    root_inode.i_uid = 30;
    target_inode.i_uid = 31;
    call_publishat(STAGED_DESCRIPTOR, TARGET_DESCRIPTOR);
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(rewrite_calls == 0);
}

static void
write_and_cleanup_failures_preserve_link_accounting(void)
{
    reset_fixture();
    inode_update_error = EIO;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(update_calls == 1);
    HK_CHECK(lookup_index == 1);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(remove_calls == 0);

    reset_fixture();
    directory_write_error = EIO;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(staged_inode.i_nlink == 0);
    HK_CHECK(remove_calls == 1);

    reset_fixture();
    directory_remove_error = EIO;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EIO);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(remove_calls == 1);

    reset_fixture();
    cleanup_lookup_inode = &replacement_inode;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == ESTALE);
    HK_CHECK(enter_calls == 0);
    HK_CHECK(staged_inode.i_nlink == 1);
    HK_CHECK(replacement_inode.i_nlink == 1);
}

static void
descriptor_and_wait_failures_release_references(void)
{
    reset_fixture();
    call_publishat(-1, -1);
    HK_CHECK(u.u_error == EBADF);
    HK_CHECK(lookup_index == 0);

    reset_fixture();
    call_publishat(OTHER_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(lookup_index == 0);

    reset_fixture();
    call_publishat(STAGED_DESCRIPTOR, -2);
    HK_CHECK(u.u_error == EINVAL);
    HK_CHECK(lookup_index == 0);

    reset_fixture();
    namespace_error = EINTR;
    call_publishat(STAGED_DESCRIPTOR, -1);
    HK_CHECK(u.u_error == EINTR);
    HK_CHECK(staged_inode.i_count == 1);
    HK_CHECK(namespace_owned == 0);
}

int
main(void)
{
    absent_destination_publishes_and_removes_stage();
    expected_destination_is_replaced();
    identity_mismatches_preserve_entries();
    parent_and_policy_failures_precede_publication();
    write_and_cleanup_failures_preserve_link_accounting();
    descriptor_and_wait_failures_release_references();
    return hk_verdict("publishat_beneath ILP32 contract");
}
