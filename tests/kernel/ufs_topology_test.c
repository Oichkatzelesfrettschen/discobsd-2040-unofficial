/*
 * ILP32 contract for serializing rename topology changes and refusing the
 * historical privileged directory forms of link and unlink.  The fixture
 * links the production syscall and namespace-gate providers.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

struct user u;
struct timeval time;
struct mount mount[NMOUNT];

extern void kernel_link(void);
extern void kernel_unlink(void);
extern void kernel_rename(void);

enum operation_kind {
    OP_NONE,
    OP_LINK_DIRECTORY,
    OP_UNLINK_DIRECTORY,
    OP_RENAME_REGULAR,
    OP_RENAME_DIRECTORY,
    OP_RENAME_MISSING,
    OP_RENAME_DESTINATION_ERROR,
    OP_RENAME_SAME_TARGET,
};

static enum operation_kind operation;
static struct inode source_inode;
static struct inode source_parent;
static struct inode destination_parent;
static struct inode existing_inode;
static unsigned namei_calls;
static unsigned direnter_calls;
static unsigned dirremove_calls;
static unsigned sleep_calls;
static unsigned wakeup_calls;
static unsigned suser_calls;
static unsigned checkpath_calls;
static unsigned directory_template_reads;
static unsigned directory_template_writes;
static int sleep_result;

int
tsleep(caddr_t channel, int priority, u_int timeout)
{
    (void)channel;
    HK_CHECK(priority == (PINOD | PCATCH));
    HK_CHECK(timeout == 0);
    sleep_calls++;
    return sleep_result;
}

void
wakeup(caddr_t channel)
{
    (void)channel;
    wakeup_calls++;
}

static void
require_namespace_gate(void)
{
    int error;

    sleep_result = EINTR;
    error = ufs_namespace_enter();
    HK_CHECK(error == EINTR);
    if (error == 0)
        ufs_namespace_exit();
}

struct inode *
topology_namei(struct nameidata *name)
{
    namei_calls++;
    if (operation >= OP_RENAME_REGULAR)
        require_namespace_gate();

    switch (operation) {
    case OP_LINK_DIRECTORY:
        if (namei_calls == 1)
            return &source_inode;
        return &existing_inode;
    case OP_UNLINK_DIRECTORY:
        name->ni_pdir = &source_parent;
        return &source_inode;
    case OP_RENAME_REGULAR:
    case OP_RENAME_DIRECTORY:
    case OP_RENAME_DESTINATION_ERROR:
    case OP_RENAME_SAME_TARGET:
        if (namei_calls == 1) {
            name->ni_pdir = &source_parent;
            name->ni_dent = (struct direct){
                .d_ino = 10,
                .d_reclen = 16,
                .d_namlen = 3,
                .d_name = "old",
            };
            return &source_inode;
        }
        if (namei_calls == 2 ||
            (operation == OP_RENAME_DIRECTORY && namei_calls == 3)) {
            name->ni_pdir = &destination_parent;
            if (operation == OP_RENAME_DESTINATION_ERROR) {
                u.u_error = EACCES;
                return NULL;
            }
            if (operation == OP_RENAME_SAME_TARGET)
                return &source_inode;
            return NULL;
        }
        name->ni_pdir = &source_parent;
        return &source_inode;
    case OP_RENAME_MISSING:
        u.u_error = ENOENT;
        return NULL;
    default:
        HK_CHECK(0);
        return NULL;
    }
}

int
suser(void)
{
    suser_calls++;
    if (u.u_uid == 0)
        return 1;
    u.u_error = EPERM;
    return 0;
}

void
iunlock(struct inode *inode)
{
    inode->i_flag &= ~ILOCKED;
}

void
iput(struct inode *inode)
{
    if (inode == NULL)
        return;
    inode->i_flag &= ~ILOCKED;
    if (inode->i_count != 0)
        inode->i_count--;
}

void
irele(struct inode *inode)
{
    if (inode->i_count != 0)
        inode->i_count--;
}

int
iupdat(struct inode *inode, struct timeval *access_time,
    struct timeval *modify_time, int waitfor)
{
    HK_CHECK(inode != NULL);
    HK_CHECK(access_time == &time && modify_time == &time);
    HK_CHECK(waitfor == 1);
    return 0;
}

int
access(struct inode *inode, int mode)
{
    (void)inode;
    (void)mode;
    return 0;
}

int
checkpath(struct inode *source, struct inode *target)
{
    HK_CHECK(operation == OP_RENAME_DIRECTORY);
    HK_CHECK(source == &source_inode);
    HK_CHECK(target == &destination_parent);
    require_namespace_gate();
    checkpath_calls++;
    return 0;
}

int
dirempty(struct inode *inode, ino_t parent)
{
    (void)inode;
    (void)parent;
    return 1;
}

void
cacheinval(struct inode *inode)
{
    (void)inode;
}

void
dirrewrite(struct inode *directory, struct inode *inode,
    struct nameidata *name)
{
    (void)directory;
    (void)inode;
    (void)name;
}

void
itrunc(struct inode *inode, off_t length, int flags)
{
    (void)inode;
    (void)length;
    (void)flags;
}

int
rdwri(enum uio_rw direction, struct inode *inode, caddr_t bytes, int length,
    off_t offset, int flags, int *residual)
{
    struct dirtemplate *directory = (struct dirtemplate *)bytes;

    HK_CHECK(operation == OP_RENAME_DIRECTORY);
    HK_CHECK(inode == &source_inode);
    HK_CHECK((unsigned)length == sizeof(*directory));
    HK_CHECK(offset == 0);
    HK_CHECK(residual == NULL);
    require_namespace_gate();
    if (direction == UIO_READ) {
        HK_CHECK(flags == IO_UNIT);
        *directory = (struct dirtemplate){
            .dot_namlen = 1,
            .dot_name = ".",
            .dotdot_namlen = 2,
            .dotdot_name = "..",
            .dotdot_ino = source_parent.i_number,
        };
        directory_template_reads++;
    } else {
        HK_CHECK(direction == UIO_WRITE);
        HK_CHECK(flags == (IO_UNIT | IO_SYNC));
        HK_CHECK(directory->dotdot_ino == destination_parent.i_number);
        directory_template_writes++;
    }
    return 0;
}

int
direnter(struct inode *inode, struct nameidata *name)
{
    HK_CHECK(inode == &source_inode);
    require_namespace_gate();
    direnter_calls++;
    iput(name->ni_pdir);
    return 0;
}

int
dirremove(struct nameidata *name)
{
    (void)name;
    require_namespace_gate();
    dirremove_calls++;
    return 1;
}

static void
reset_fixture(enum operation_kind requested_operation, uid_t user_id)
{
    u = (struct user){ .u_uid = user_id };
    time = (struct timeval){ .tv_sec = 1234, .tv_usec = 0 };
    mount[0] = (struct mount){ .m_dev = 3 };
    source_inode = (struct inode){
        .i_flag = ILOCKED,
        .i_count = 2,
        .i_number = 10,
        .i_mode = requested_operation == OP_RENAME_DIRECTORY ? IFDIR :
            requested_operation >= OP_RENAME_REGULAR ? IFREG : IFDIR,
        .i_nlink = 1,
    };
    source_parent = (struct inode){
        .i_flag = ILOCKED, .i_count = 2, .i_number = 2, .i_mode = IFDIR,
    };
    destination_parent = (struct inode){
        .i_flag = ILOCKED, .i_count = 2, .i_number = 3, .i_mode = IFDIR,
    };
    existing_inode = (struct inode){
        .i_flag = ILOCKED, .i_count = 1, .i_number = 11, .i_mode = IFREG,
    };
    operation = requested_operation;
    namei_calls = 0;
    direnter_calls = 0;
    dirremove_calls = 0;
    sleep_calls = 0;
    wakeup_calls = 0;
    suser_calls = 0;
    checkpath_calls = 0;
    directory_template_reads = 0;
    directory_template_writes = 0;
    sleep_result = 0;
}

static void
directory_link_is_rejected(uid_t user_id)
{
    reset_fixture(OP_LINK_DIRECTORY, user_id);
    kernel_link();
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(namei_calls == 1);
    HK_CHECK(direnter_calls == 0);
    HK_CHECK(suser_calls == 0);
}

static void
directory_unlink_is_rejected(uid_t user_id)
{
    reset_fixture(OP_UNLINK_DIRECTORY, user_id);
    kernel_unlink();
    HK_CHECK(u.u_error == EPERM);
    HK_CHECK(namei_calls == 1);
    HK_CHECK(dirremove_calls == 0);
    HK_CHECK(suser_calls == 0);
}

static void
set_rename_arguments(void)
{
    struct {
        char *from;
        char *to;
    } arguments = { "old", "new" };

    bcopy(&arguments, u.u_arg, sizeof(arguments));
}

static void
require_gate_free(void)
{
    sleep_result = EINTR;
    HK_CHECK(ufs_namespace_enter() == 0);
    ufs_namespace_exit();
}

static void
rename_holds_gate_through_source_removal(void)
{
    reset_fixture(OP_RENAME_REGULAR, 100);
    set_rename_arguments();
    kernel_rename();
    HK_CHECK(u.u_error == 0);
    HK_CHECK(namei_calls == 3);
    HK_CHECK(direnter_calls == 1);
    HK_CHECK(dirremove_calls == 1);
    HK_CHECK(sleep_calls == 5);
    HK_CHECK(wakeup_calls == 1);

    require_gate_free();
}

static void
directory_rename_holds_gate_through_relocation(void)
{
    reset_fixture(OP_RENAME_DIRECTORY, 100);
    set_rename_arguments();
    kernel_rename();
    HK_CHECK(u.u_error == 0);
    HK_CHECK(namei_calls == 4);
    HK_CHECK(checkpath_calls == 1);
    HK_CHECK(direnter_calls == 1);
    HK_CHECK(dirremove_calls == 1);
    HK_CHECK(directory_template_reads == 1);
    HK_CHECK(directory_template_writes == 1);
    HK_CHECK((source_inode.i_flag & IRENAME) == 0);

    require_gate_free();
}

static void
rename_releases_gate_after_lookup_errors(void)
{
    reset_fixture(OP_RENAME_MISSING, 100);
    set_rename_arguments();
    kernel_rename();
    HK_CHECK(u.u_error == ENOENT);
    HK_CHECK(namei_calls == 1);
    require_gate_free();

    reset_fixture(OP_RENAME_DESTINATION_ERROR, 100);
    set_rename_arguments();
    kernel_rename();
    HK_CHECK(u.u_error == EACCES);
    HK_CHECK(namei_calls == 2);
    require_gate_free();

    reset_fixture(OP_RENAME_SAME_TARGET, 100);
    set_rename_arguments();
    kernel_rename();
    HK_CHECK(u.u_error == 0);
    HK_CHECK(namei_calls == 2);
    require_gate_free();
}

static void
rename_preserves_owner_when_gate_wait_is_interrupted(void)
{
    reset_fixture(OP_RENAME_REGULAR, 100);
    set_rename_arguments();
    HK_CHECK(ufs_namespace_enter() == 0);
    sleep_result = EINTR;
    kernel_rename();
    HK_CHECK(u.u_error == EINTR);
    HK_CHECK(namei_calls == 0);
    ufs_namespace_exit();
    require_gate_free();
}

int
main(void)
{
    directory_link_is_rejected(0);
    directory_link_is_rejected(100);
    directory_unlink_is_rejected(0);
    directory_unlink_is_rejected(100);
    rename_holds_gate_through_source_removal();
    directory_rename_holds_gate_through_relocation();
    rename_releases_gate_after_lookup_errors();
    rename_preserves_owner_when_gate_wait_is_interrupted();
    return hk_verdict("UFS topology mutation serialization");
}
