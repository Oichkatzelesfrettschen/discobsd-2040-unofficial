/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Drive the kernel call chains the static stack bound of
 * docs/research/kernel-size-flags names, one stage per invocation.
 * kern.capacity reports the deepest kernel stack since boot and lacks a
 * reset, so a stage's own depth is the reading after that stage alone on a
 * fresh boot; in a sequence of stages a reading bounds every stage so far
 * and only a rise belongs to the stage that produced it.
 *
 *	stackchain [-a] [-d dir] [-t tty] stage
 *
 *	churn N		write and remove a 64 KiB file N times, which wraps
 *			the Dhara journal and starts its garbage collection
 *	file		a child exits holding an unlinked file with indirect
 *			blocks, beside a dirty buffer
 *	exec		a traced child stops output on the terminal, queues
 *			bytes there on a close-on-exec descriptor, holds the
 *			same kind of file and executes a script; the close
 *			execve makes then waits for the drain, the trace
 *			signal execve posts stops the child inside that wait
 *			and the parent's PT_KILL ends it there; the parent
 *			then restarts the output so the dying child's own
 *			close returns
 *	execplain	the same without the file, as the control
 *
 * The files go under dir (/tmp unless -d names another). The Dhara chain
 * runs only when that directory is on /dev/fl0a, so a stage refuses any
 * other device with exit 4 unless -a accepts it; storage_dev and flash_root
 * in the output name the device either way. On a root that is not the
 * flash, such as the PicoCalc SD root, -d must name a mounted flash file
 * system for the Dhara chain, and -a measures the chain of the device that
 * is there.
 *
 * The drain wait happens only in the close that drops the terminal's last
 * open reference. The terminal is /dev/tty0 unless -t names another, and it
 * must be one that the login, getty and standard streams of the run leave
 * closed. An exec stage passes with stage_ok=1: the child stopped by
 * SIGTRAP, killed by PT_KILL and dead by signal, the parent's restart of
 * the output accepted, and the script not run.
 *
 * A file stage that succeeds leaves its directory, holding the dirty file,
 * and prints it as left_dir: read kern.capacity first, then remove it.
 *
 * Every step prints one key=value line. A stage exits 0 only when each
 * condition it exists to produce held: the churn rounds written and
 * removed, or the file unlinked and written and the dirty buffer left.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BLOCK		1024L
#define	NDIRECT		4L
#define	NPOINTER	256L	/* block addresses in one indirect block */
/*
 * The files live in a directory this run creates with mode 0700 and enters,
 * so no name in the world-writable /tmp is opened or executed.
 */
#define	DEEP		"sc.deep"
#define	DIRTY		"sc.dirty"
#define	SCRIPT		"./sc.sh"
#define	MARK		"sc.ran"
#define	START_TRIES	10	/* restarts of the output the parent attempts */
#define	HELPER_SECONDS	60	/* bound on a stage that never stops the child */

static char block[BLOCK];
static char private_dir[96];
static const char *private_name;	/* its last component */
static const char *tty_name = "/dev/tty0";

/*
 * Create and enter the run's directory under parent; mkdir fails on an
 * existing name.
 */
static int
enter_private_dir(const char *parent)
{
	int n = snprintf(private_dir, sizeof private_dir, "%s/stackchain.%d",
	    parent, (int)getpid());

	if (n < 0 || n >= (int)sizeof private_dir) {
		printf("private_dir_too_long=1\n");
		return -1;
	}
	private_name = strrchr(private_dir, '/') + 1;
	if (mkdir(private_dir, 0700) < 0 || chdir(private_dir) < 0) {
		printf("private_dir_errno=%d\n", errno);
		return -1;
	}
	return 0;
}

static int
leave_private_dir(int status)
{
	(void)unlink(DEEP);
	(void)unlink(DIRTY);
	(void)unlink(SCRIPT);
	(void)unlink(MARK);
	/* By way of the parent, so that a relative -d still names it. */
	if (chdir("..") < 0 || rmdir(private_name) < 0) {
		printf("private_dir_remove_errno=%d\n", errno);
		return 1;
	}
	return status;
}

/*
 * Name the device under the run's directory and say whether it is the
 * Dhara root partition: 1 when it is /dev/fl0a, 0 when it is another
 * device, -1 when the directory's device could not be read.
 */
static int
on_flash_root(void)
{
	struct stat dir, flash;
	int same;

	if (stat(".", &dir) < 0) {
		printf("storage_stat_errno=%d\n", errno);
		return -1;
	}
	same = stat("/dev/fl0a", &flash) == 0 &&
	    (flash.st_mode & S_IFMT) == S_IFBLK && flash.st_rdev == dir.st_dev;
	printf("storage_dev=%d,%d flash_root=%d\n", (int)major(dir.st_dev),
	    (int)minor(dir.st_dev), same);
	return same;
}

/* waitpid that a signal to this process does not cut short. */
static pid_t
wait_for(pid_t pid, int *status, int options)
{
	pid_t got;

	do
		got = waitpid(pid, status, options);
	while (got < 0 && errno == EINTR);
	return got;
}

/*
 * An unlinked file whose last block sits behind as many indirection levels
 * as the file system accepts, three first. The result is the descriptor,
 * and the level reached is printed.
 */
static int
deep_file(void)
{
	static const long first[] = {
		(NDIRECT + NPOINTER + NPOINTER * NPOINTER) * BLOCK,
		(NDIRECT + NPOINTER) * BLOCK,
		NDIRECT * BLOCK,
	};
	int fd, i, failed;

	for (i = 0; i < 3; i++) {
		fd = open(DEEP, O_RDWR | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			printf("deep_open_errno=%d\n", errno);
			return -1;
		}
		if (unlink(DEEP) < 0) {
			printf("deep_unlink_errno=%d\n", errno);
			(void)close(fd);
			return -1;
		}
		if (lseek(fd, (off_t)first[i], SEEK_SET) == (off_t)first[i] &&
		    write(fd, block, sizeof block) == (int)sizeof block) {
			printf("deep_levels=%d\n", 3 - i);
			return fd;
		}
		failed = errno;
		printf("deep_level_%d_errno=%d\n", 3 - i, failed);
		(void)close(fd);
		/*
		 * Only a size the file system refuses outright, before it
		 * allocates anything, leads to the next level down; any other
		 * failure may have left blocks behind and ends the stage.
		 */
		if (failed != EFBIG)
			return -1;
	}
	return -1;
}

/*
 * Leave a delayed-write buffer for the next buffer claim to flush; -1 when
 * the write that makes it failed.
 */
static int
dirty_buffer(void)
{
	int fd = open(DIRTY, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	/*
	 * Half a block: ino_rw() starts the write of a whole block at once
	 * and leaves only a partial one as a delayed-write buffer.
	 */
	int ok = fd >= 0 && write(fd, block, sizeof block / 2) ==
	    (int)(sizeof block / 2);

	if (!ok)
		printf("dirty_errno=%d\n", errno);
	if (fd >= 0)
		(void)close(fd);
	return ok ? 0 : -1;
}

static int
churn(int rounds)
{
	int fd, i, k;

	for (i = 0; i < rounds; i++) {
		fd = open(DIRTY, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			printf("churn_open_errno=%d\n", errno);
			return 1;
		}
		memset(block, 'a' + i % 26, sizeof block);
		for (k = 0; k < 64; k++)
			if (write(fd, block, sizeof block) != (int)sizeof block) {
				printf("churn_write_errno=%d\n", errno);
				(void)close(fd);
				return 1;
			}
		/* sync() returns void; fsync() returns a failed write. */
		if (fsync(fd) < 0) {
			printf("churn_fsync_errno=%d\n", errno);
			(void)close(fd);
			return 1;
		}
		(void)close(fd);
		if (unlink(DIRTY) < 0) {
			printf("churn_unlink_errno=%d\n", errno);
			return 1;
		}
		/*
		 * The unlink queues inode and free-list writes of its own.
		 * fsync() of the directory flushes the file system and returns
		 * a write or checkpoint that failed, which sync() cannot.
		 */
		fd = open(".", O_RDONLY);
		if (fd < 0 || fsync(fd) < 0) {
			printf("churn_flush_errno=%d\n", errno);
			if (fd >= 0)
				(void)close(fd);
			return 1;
		}
		(void)close(fd);
	}
	printf("churn_rounds=%d\n", rounds);
	return 0;
}

/*
 * The exit under measurement is a child's, so that the parent reports its
 * status and, when the stage failed, removes the run's directory.
 */
static int
stage_file(void)
{
	int status;
	pid_t pid = fork();

	if (pid < 0) {
		printf("fork_errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		if (deep_file() < 0 || dirty_buffer() < 0)
			_exit(1);
		printf("file_exit=1\n");
		_exit(0);
	}
	if (wait_for(pid, &status, 0) != pid) {
		printf("wait_errno=%d\n", errno);
		return 1;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}

static int
stage_exec(int deep)
{
	static const char script[] = "#!/bin/sh\necho ran >" MARK "\n";
	int fd, tty, status, ok, started, held, tries;
	pid_t pid, helper, got;

	(void)unlink(MARK);
	fd = open(SCRIPT, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0 || write(fd, script, sizeof script - 1) !=
	    (int)(sizeof script - 1)) {
		printf("script_errno=%d\n", errno);
		return 1;
	}
	(void)close(fd);
	fflush(stdout);
	/*
	 * The child dies inside its own exit while that exit closes the
	 * stopped terminal, which would wait for ever, so the parent restarts
	 * the output after it has seen the traced stop and sent PT_KILL. The
	 * helper is the bound on a stage that never reaches that point: after
	 * HELPER_SECONDS it restarts the output itself and exits 3, which
	 * fails the stage.
	 */
	helper = fork();
	if (helper == 0) {
		sleep(HELPER_SECONDS);
		tty = open(tty_name, O_WRONLY | O_NONBLOCK);
		printf("helper_timeout=1 helper_start=%d\n",
		    tty >= 0 && ioctl(tty, TIOCSTART, 0) == 0);
		_exit(3);
	}
	if (helper < 0) {
		printf("helper_fork_errno=%d\n", errno);
		(void)unlink(SCRIPT);
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		printf("fork_errno=%d\n", errno);
		(void)kill(helper, SIGKILL);
		(void)wait_for(helper, &status, 0);
		(void)unlink(SCRIPT);
		return 1;
	}
	if (pid == 0) {
		if (ptrace(PT_TRACE_ME, 0, 0, 0) < 0 && errno != 0)
			printf("trace_errno=%d\n", errno);
		tty = open(tty_name, O_WRONLY);
		if (tty < 0 || fcntl(tty, F_SETFD, 1) < 0 ||
		    ioctl(tty, TIOCSTOP, 0) < 0 ||
		    write(tty, "stackchain\r\n", 12) != 12) {
			printf("tty_errno=%d\n", errno);
			fflush(stdout);
			_exit(8);
		}
		if (deep) {
			if (deep_file() < 0 || dirty_buffer() < 0)
				_exit(7);
		}
		printf("child_exec=1 deep=%d\n", deep);
		fflush(stdout);
		execl(SCRIPT, "sc.sh", (char *)0);
		printf("exec_errno=%d\n", errno);
		fflush(stdout);
		_exit(9);
	}
	if (wait_for(pid, &status, WUNTRACED) != pid) {
		printf("wait_errno=%d\n", errno);
		return 1;
	}
	printf("child_stopped=%d stop_signal=%d\n", WIFSTOPPED(status) != 0,
	    WIFSTOPPED(status) ? WSTOPSIG(status) : 0);
	ok = WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP;
	errno = 0;
	(void)ptrace(PT_KILL, pid, 0, 0);
	printf("ptkill_errno=%d\n", errno);
	if (errno != 0) {
		/* The stopped child would otherwise never be reaped. */
		ok = 0;
		(void)kill(pid, SIGKILL);
	}
	/*
	 * held_in_close reports whether the killed child is still alive a
	 * second later. SIGKILL interrupts the drain wait, so 0 is the usual
	 * reading; 1 names a child that waits for the output to restart, or
	 * a terminal the run holds open elsewhere.
	 */
	sleep(1);
	got = wait_for(pid, &status, WNOHANG);
	held = got == 0;
	printf("held_in_close=%d\n", held);
	/*
	 * The child cannot die until the output restarts, so the restart is
	 * tried START_TRIES times a second apart, and a child that is still
	 * held after the last is reported and left, not waited for.
	 */
	started = 0;
	for (tries = 0; tries < START_TRIES && !started; tries++) {
		if (tries > 0)
			sleep(1);
		tty = open(tty_name, O_WRONLY | O_NONBLOCK);
		started = tty >= 0 && ioctl(tty, TIOCSTART, 0) == 0;
		if (tty >= 0)
			(void)close(tty);
	}
	printf("parent_start=%d\n", started);
	if (!started) {
		ok = 0;
		if (got != pid)
			got = wait_for(pid, &status, WNOHANG);
		if (got != pid)
			printf("child_left_stopped=1\n");
	}
	if (!started && got != pid)
		;
	else if (got != pid && wait_for(pid, &status, 0) != pid) {
		printf("wait2_errno=%d\n", errno);
		ok = 0;
	} else {
		printf("child_signaled=%d child_exited=%d\n",
		    WIFSIGNALED(status) != 0, WIFEXITED(status) != 0);
		if (!WIFSIGNALED(status))
			ok = 0;
	}
	/* The script's mark is absent when the child died inside execve. */
	printf("script_ran=%d\n", access(MARK, 0) == 0);
	if (access(MARK, 0) == 0)
		ok = 0;
	(void)kill(helper, SIGKILL);
	if (wait_for(helper, &status, 0) != helper || !WIFSIGNALED(status))
		ok = 0;
	(void)unlink(SCRIPT);
	(void)unlink(DIRTY);
	printf("stage_ok=%d\n", ok);
	return ok ? 0 : 1;
}

static void
usage(void)
{
	fprintf(stderr, "usage: stackchain [-a] [-d dir] [-t tty] "
	    "churn N | file | exec | execplain\n");
}

int
main(int argc, char **argv)
{
	const char *parent = "/tmp";
	long rounds = 0;
	char *end;
	int stage, c, any = 0, flash, status;

	setvbuf(stdout, (char *)0, _IONBF, 0);
	while ((c = getopt(argc, argv, "ad:t:")) != -1)
		switch (c) {
		case 'a':
			any = 1;
			break;
		case 'd':
			parent = optarg;
			break;
		case 't':
			tty_name = optarg;
			break;
		default:
			usage();
			return 2;
		}
	argc -= optind;
	argv += optind;
	if (argc == 2 && strcmp(argv[0], "churn") == 0) {
		rounds = strtol(argv[1], &end, 10);
		if (end == argv[1] || *end != '\0' || rounds <= 0 ||
		    rounds > 10000) {
			fprintf(stderr, "stackchain: churn count 1 to 10000\n");
			return 2;
		}
		stage = 0;
	} else if (argc == 1 && strcmp(argv[0], "file") == 0)
		stage = 1;
	else if (argc == 1 && strcmp(argv[0], "exec") == 0)
		stage = 2;
	else if (argc == 1 && strcmp(argv[0], "execplain") == 0)
		stage = 3;
	else {
		usage();
		return 2;
	}
	if (enter_private_dir(parent) < 0)
		return 1;
	flash = on_flash_root();
	if (flash < 0)
		/* A device that cannot be named is not measured, -a or not. */
		return leave_private_dir(1);
	/* execplain runs without a file, so its device is reported, not required. */
	if (!flash && !any && stage != 3) {
		printf("refused=not_flash_root\n");
		(void)leave_private_dir(4);
		return 4;
	}
	if (stage == 0)
		return leave_private_dir(churn((int)rounds));
	if (stage >= 2)
		return leave_private_dir(stage_exec(stage == 2));
	/*
	 * Removing the dirty file is a file system operation with a stack
	 * chain of its own, and the watermark is read after this program
	 * ends, so a file stage that succeeded leaves its directory and
	 * names it for removal once kern.capacity has been read.
	 */
	status = stage_file();
	if (status != 0)
		return leave_private_dir(status);
	printf("left_dir=%s\n", private_dir);
	return 0;
}
