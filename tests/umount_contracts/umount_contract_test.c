/*
 * Host gate for sbin/umount, linked from the program's own source against a
 * mount table and a umount(2) this file supplies.
 *
 * The property under test is the exit status of a multi-operand run.
 * umountfs() reports a failed operand -- every path that unmounts returns 0
 * and every path that refuses returns 1 -- and umountall() reads it that way
 * when it folds "umountfs(cp) || rval". main() has to fold it the same way,
 * because the exit status is what a script branches on and nothing else in
 * the program records the outcome.
 *
 * Nothing here touches a real filesystem: stat(), getmntinfo() and umount()
 * are defined below and the linker prefers them to the host's, so the gate
 * names the whole mount table and decides which operands fail. main() and
 * exit() are renamed by the Makefile, so the gate calls one and catches the
 * other.
 *
 * This file carries the tree's headers alone; umount_shim.h says why.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fstab.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>

#include "umount_shim.h"

int	umount_main(int argc, char *argv[]);
void	umount_exit(int status);

/* umount.c's own flags, reset between runs so one case cannot reach another. */
extern int	fake, fflag, vflag, allflag, *typelist;
extern char	*nfshost;

/*
 * The mount table the gate serves. Two filesystems are mounted; a third name
 * is a directory that is not a mount point, and a fourth does not exist.
 */
#define MNT_A_FROM	"/dev/fl0a"
#define MNT_A_ON	"/mnt/a"
#define MNT_B_FROM	"/dev/fl0b"
#define MNT_B_ON	"/mnt/b"
#define NOT_MOUNTED	"/mnt/loose"
#define NO_SUCH		"/mnt/absent"

static struct statfs table[2];
static int table_size;

/* Mount points whose umount(2) is made to fail, by mounted-from name. */
static const char *fail_from[4];
static unsigned nfail;

static unsigned umount_calls;

static void
reset(void)
{
	memset(table, 0, sizeof(table));
	strcpy(table[0].f_mntfromname, MNT_A_FROM);
	strcpy(table[0].f_mntonname, MNT_A_ON);
	table[0].f_type = MOUNT_UFS;
	strcpy(table[1].f_mntfromname, MNT_B_FROM);
	strcpy(table[1].f_mntonname, MNT_B_ON);
	table[1].f_type = MOUNT_UFS;
	table_size = 2;

	memset(fail_from, 0, sizeof(fail_from));
	nfail = 0;
	umount_calls = 0;

	fake = 0;
	fflag = 0;
	vflag = 0;
	allflag = 0;
	typelist = NULL;
	nfshost = NULL;
	optind = 1;
}

static void
make_fail(const char *from)
{
	fail_from[nfail++] = from;
}

int
getmntinfo(struct statfs **mntbufp, int flags)
{
	(void)flags;
	*mntbufp = table;
	return (table_size);
}

/*
 * stat() over the gate's own name space: the two mount points and the loose
 * directory are directories, the devices are block special, and every other
 * name is absent. umountfs() branches on exactly these three answers.
 */
int
stat(const char *path, struct stat *sb)
{
	memset(sb, 0, sizeof(*sb));
	if (strcmp(path, MNT_A_ON) == 0 || strcmp(path, MNT_B_ON) == 0 ||
	    strcmp(path, NOT_MOUNTED) == 0) {
		sb->st_mode = S_IFDIR | 0755;
		return (0);
	}
	if (strcmp(path, MNT_A_FROM) == 0 || strcmp(path, MNT_B_FROM) == 0) {
		sb->st_mode = S_IFBLK | 0600;
		return (0);
	}
	return (-1);
}

int
umount(const char *name)
{
	unsigned i;

	umount_calls++;
	for (i = 0; i < nfail; i++)
		if (fail_from[i] != NULL && strcmp(fail_from[i], name) == 0)
			return (-1);
	return (0);
}

/* The program calls these; none of them reaches a device here. */
void
sync(void)
{
}

int
setfsent(void)
{
	return (0);
}

struct fstab *
getfsent(void)
{
	return (NULL);
}

/*
 * include/stdio.h spells stderr as &_iob[2], so usage() takes the address of
 * a stream the host's libc does not own. The storage exists for that address;
 * the renamed output calls below never read it.
 */
struct _iobuf _iob[3];

int
umount_printf(const char *fmt, ...)
{
	(void)fmt;
	return (0);
}

int
umount_fprintf(FILE *stream, const char *fmt, ...)
{
	(void)stream;
	(void)fmt;
	return (0);
}

/* err(3), which the program reports through and the gate keeps quiet. */
void
warn(const char *fmt, ...)
{
	(void)fmt;
}

void
warnx(const char *fmt, ...)
{
	(void)fmt;
}

void
err(int status, const char *fmt, ...)
{
	(void)fmt;
	umount_exit(status);
}

void
errx(int status, const char *fmt, ...)
{
	(void)fmt;
	umount_exit(status);
}

/*
 * main() ends in exit(), which the Makefile renames so the gate can read the
 * status rather than lose the process.
 */
static jmp_buf exit_jmp;
static int exit_status;

void
umount_exit(int status)
{
	exit_status = status;
	longjmp(exit_jmp, 1);
}

/* Run umount with the given operands and answer with its exit status. */
static int
run(char *argv[], int argc)
{
	if (setjmp(exit_jmp) == 0) {
		(void)umount_main(argc, argv);
		/* main() always ends in exit(); reaching here is a defect. */
		return (-1);
	}
	return (exit_status);
}

/*
 * One operand that unmounts. The status is success, which is the case the
 * inverted fold reported as failure.
 */
static void
test_one_operand_succeeds(void)
{
	char *av[] = { "umount", MNT_A_ON, NULL };

	reset();
	CHECK(run(av, 2) == 0);
	CHECK(umount_calls == 1);
}

/* One operand that is not mounted. Nothing is unmounted and the status says so. */
static void
test_one_operand_fails(void)
{
	char *av[] = { "umount", NOT_MOUNTED, NULL };

	reset();
	CHECK(run(av, 2) != 0);
	CHECK(umount_calls == 0);
}

/* A name that does not exist at all reaches the same verdict. */
static void
test_absent_operand_fails(void)
{
	char *av[] = { "umount", NO_SUCH, NULL };

	reset();
	CHECK(run(av, 2) != 0);
	CHECK(umount_calls == 0);
}

/* Every operand unmounts, so the status is success and both were tried. */
static void
test_all_operands_succeed(void)
{
	char *av[] = { "umount", MNT_A_ON, MNT_B_ON, NULL };

	reset();
	CHECK(run(av, 3) == 0);
	CHECK(umount_calls == 2);
}

/*
 * One operand of several fails. The run continues past it -- both are tried
 * -- and the status reports the failure.
 */
static void
test_one_of_several_fails(void)
{
	char *av[] = { "umount", NOT_MOUNTED, MNT_B_ON, NULL };

	reset();
	CHECK(run(av, 3) != 0);
	CHECK(umount_calls == 1);	/* the loose directory never reaches it */
}

/* The failure may come from umount(2) itself rather than from the lookup. */
static void
test_syscall_failure_is_reported(void)
{
	char *av[] = { "umount", MNT_A_ON, MNT_B_ON, NULL };

	reset();
	make_fail(MNT_A_FROM);
	CHECK(run(av, 3) != 0);
	CHECK(umount_calls == 2);	/* the second is still attempted */
}

/* A device name resolves to its mount point and unmounts the same way. */
static void
test_device_operand(void)
{
	char *av[] = { "umount", MNT_A_FROM, NULL };

	reset();
	CHECK(run(av, 2) == 0);
	CHECK(umount_calls == 1);
}

/*
 * -F names the run that changes nothing. Every operand is reported as
 * unmounted and umount(2) is never reached, so the status is success.
 */
static void
test_fake_run_succeeds(void)
{
	char *av[] = { "umount", "-F", MNT_A_ON, MNT_B_ON, NULL };

	reset();
	CHECK(run(av, 4) == 0);
	CHECK(umount_calls == 0);
}

/*
 * "-t noufs" excludes the only type there is, so every operand is
 * unselected. umountfs() reports an unselected operand as success, so the
 * status is success and nothing is unmounted.
 */
static void
test_unselected_type_succeeds(void)
{
	char *av[] = { "umount", "-t", "noufs", MNT_A_ON, NULL };

	reset();
	CHECK(run(av, 4) == 0);
	CHECK(umount_calls == 0);
}

/* A type no filesystem is named by is refused before any operand is reached. */
static void
test_unknown_type_is_refused(void)
{
	char *av[] = { "umount", "-t", "nosuchfs", MNT_A_ON, NULL };

	reset();
	CHECK(run(av, 4) != 0);
	CHECK(umount_calls == 0);
}

/*
 * The type list is asked once per operand, so it has to survive the first
 * answer. Only an operand whose type the list does not name walks the list
 * to its terminator, and only an operand after it can show whether the list
 * is still there: the first mount here is of a type "ufs" does not name and
 * the second is of one it does, so the second is selected and unmounts. A
 * walk that advanced typelist itself would leave the second facing an empty
 * list, which answers "not selected" for every type, and nothing would
 * unmount at all.
 *
 * MOUNT_MAXTYPE is 1 in sys/mount.h, so a running system mounts nothing of
 * the first kind; the gate names the type a second filesystem would report.
 */
static void
test_type_list_survives_each_operand(void)
{
	char *av[] = { "umount", "-t", "ufs", MNT_A_ON, MNT_B_ON, NULL };

	reset();
	table[0].f_type = MOUNT_UFS + 1;	/* walks the list to its end */
	table[1].f_type = MOUNT_UFS;		/* named by the list */
	CHECK(run(av, 5) == 0);
	CHECK(umount_calls == 1);
}

int
main(void)
{
	test_one_operand_succeeds();
	test_one_operand_fails();
	test_absent_operand_fails();
	test_all_operands_succeed();
	test_one_of_several_fails();
	test_syscall_failure_is_reported();
	test_device_operand();
	test_fake_run_succeeds();
	test_unselected_type_succeeds();
	test_unknown_type_is_refused();
	test_type_list_survives_each_operand();
	return (ushim_verdict("umount"));
}
