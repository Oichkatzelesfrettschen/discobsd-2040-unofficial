/*
 * fstat
 *
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/*
 * fstat lists open files from the kernel's records: KERN_PROC names each
 * process, and KERN_FILE returns its working directory and descriptors
 * (sys/sysctl.h). The kernel reads a descriptor table wherever the image
 * lives and returns it to root and to a caller whose effective uid is the
 * process's real or effective uid, so fstat reads no memory device and
 * needs no privilege; it lists the processes the kernel admits it to.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A file named on the command line, or a block device's whole file system. */
struct devs {
	struct devs	*next;
	dev_t		dev;
	ino_t		inum;		/* 0 matches every inode on dev */
	const char	*name;
};

static struct devs *devs;
static int vflg;

static const char *
itype(unsigned mode)
{
	switch (mode & S_IFMT) {
	case S_IFCHR:
		return ("chr");
	case S_IFDIR:
		return ("dir");
	case S_IFBLK:
		return ("blk");
	case S_IFREG:
		return ("reg");
	case S_IFLNK:
		return ("lnk");
	case S_IFSOCK:
		return ("soc");
	default:
		return ("unk");
	}
}

static int
devmatch(dev_t dev, ino_t inum, const char **name)
{
	const struct devs *d;

	for (d = devs; d != NULL; d = d->next) {
		if (d->dev == dev && (d->inum == 0 || d->inum == inum)) {
			*name = d->name;
			return (1);
		}
	}
	return (0);
}

static int
getfname(const char *filename)
{
	struct stat st;
	struct devs *cur;

	if (stat(filename, &st) != 0) {
		perror(filename);
		return (0);
	}
	if ((cur = malloc(sizeof(*cur))) == NULL) {
		fputs("fstat: out of space.\n", stderr);
		exit(1);
	}
	/* A block special file stands for open files on that device. */
	if ((st.st_mode & S_IFMT) == S_IFBLK) {
		cur->dev = st.st_rdev;
		cur->inum = 0;
	} else {
		cur->dev = st.st_dev;
		cur->inum = st.st_ino;
	}
	cur->name = filename;
	cur->next = devs;
	devs = cur;
	return (1);
}

static void
show_files(const struct kinfo_proc *kp, const char *user)
{
	static struct kinfo_file kf[NOFILE + 1];
	int mib[5] = { CTL_KERN, KERN_FILE, KERN_FILE_BYPID, kp->kp_pid,
	    sizeof(kf[0]) };
	const char *comm = (kp->kp_valid & KP_VALID_COMM) ? kp->kp_comm : "-";
	size_t len = sizeof(kf);

	if (sysctl(mib, 5, kf, &len, NULL, 0) < 0) {
		if (vflg)
			fprintf(stderr, "fstat: pid %d: %s\n", kp->kp_pid,
			    strerror(errno));
		return;
	}
	for (size_t i = 0; i < len / sizeof(kf[0]); i++) {
		const struct kinfo_file *f = &kf[i];
		unsigned inode = f->kf_valid & KF_VALID_INODE;
		const char *name = NULL;

		if (devs != NULL &&
		    (!inode || !devmatch(f->kf_dev, f->kf_inum, &name)))
			continue;
		printf("%-8.8s %-10.10s %5d  ", user, comm, kp->kp_pid);
		if (f->kf_fd == KF_FD_CWD)
			printf("  wd");
		else
			printf("%4d", f->kf_fd);
		if (inode)
			printf("\t%2d, %2d\t%5lu\t%6ld\t%3s %s\n",
			    major(f->kf_dev), minor(f->kf_dev),
			    (unsigned long)f->kf_inum,
			    (f->kf_mode & S_IFMT) == S_IFSOCK ? 0L :
			    (long)f->kf_isize,
			    f->kf_type == DTYPE_PIPE ? "pip" : itype(f->kf_mode),
			    name != NULL ? name : "");
		else if (f->kf_type == DTYPE_SOCKET)
			printf("* (socket)\n");
		else
			printf("* (unknown file type)\n");
	}
}

static void
usage(void)
{
	fputs("usage: fstat [-v] [-u user] [-p pid] [filename ...]\n", stderr);
	exit(1);
}

int
main(int argc, char **argv)
{
	int mib[5] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0,
	    sizeof(struct kinfo_proc) };
	struct kinfo_proc *procs;
	const struct passwd *pw;
	const char *selected_user = NULL;
	uid_t uid = 0;
	long pid = -1;
	size_t len, n;
	char *end;
	int ch;

	while ((ch = getopt(argc, argv, "p:u:v")) != -1) {
		switch (ch) {
		case 'p':
			if (pid >= 0)
				usage();
			errno = 0;
			pid = strtol(optarg, &end, 10);
			if (optarg[0] < '0' || optarg[0] > '9' || *end != '\0' ||
			    errno != 0 || pid > 32767) {
				fputs("fstat: -p option requires a process id.\n",
				    stderr);
				usage();
			}
			break;
		case 'u':
			if (selected_user != NULL)
				usage();
			if ((pw = getpwnam(optarg)) == NULL) {
				fprintf(stderr, "%s: unknown uid\n", optarg);
				exit(1);
			}
			uid = pw->pw_uid;
			selected_user = pw->pw_name;
			break;
		case 'v':	/* print why a process was skipped */
			vflg = 1;
			break;
		default:
			usage();
		}
	}
	argv += optind;
	if (*argv != NULL) {
		int named = 0;

		for (; *argv != NULL; argv++)
			named |= getfname(*argv);
		if (!named)	/* file(s) specified, but none accessible */
			exit(1);
	}

	if (sysctl(mib, 5, NULL, &len, NULL, 0) < 0 ||
	    (procs = malloc(len)) == NULL ||
	    sysctl(mib, 5, procs, &len, NULL, 0) < 0) {
		perror("fstat: kern.proc");
		exit(1);
	}

	printf("USER\t CMD\t      PID    FD\tDEVICE\tINODE\t  SIZE TYPE%s\n",
	    devs != NULL ? " NAME" : "");
	for (n = 0; n < len / sizeof(*procs); n++) {
		const struct kinfo_proc *kp = &procs[n];
		const char *user = selected_user;

		if (pid >= 0 && kp->kp_pid != pid)
			continue;
		if (selected_user != NULL && kp->kp_uid != uid)
			continue;
		if (user == NULL)
			user = (pw = getpwuid(kp->kp_uid)) != NULL ?
			    pw->pw_name : "unknown";
		show_files(kp, user);
	}
	exit(0);
}
