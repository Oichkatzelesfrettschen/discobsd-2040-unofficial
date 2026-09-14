/*
 * Tiny applets whose standalone shell scripts each consume a filesystem
 * block. Adminbox hosts the applets because they define no writable static
 * storage.
 */

#include <sys/resource.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int
true_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

int
false_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 1;
}

static int
write_notice(void)
{
	static const char notice[] = "Sending output to 'nohup.out'\n";
	const char *remaining_notice;
	size_t remaining_length;

	remaining_notice = notice;
	remaining_length = sizeof(notice) - 1;
	while (remaining_length != 0) {
		ssize_t bytes_written;

		bytes_written = write(STDOUT_FILENO, remaining_notice,
		    remaining_length);
		if (bytes_written < 0) {
			if (errno == EINTR)
				continue;
			perror("nohup: stdout");
			return -1;
		}
		remaining_notice += bytes_written;
		remaining_length -= (size_t)bytes_written;
	}
	return 0;
}

static int
redirect_terminal_output(void)
{
	int output_fd;

	if (!isatty(STDOUT_FILENO))
		return 0;
	if (write_notice() < 0)
		return -1;
	output_fd = open("nohup.out", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (output_fd < 0) {
		perror("nohup.out");
		return -1;
	}
	if (dup2(output_fd, STDOUT_FILENO) < 0) {
		perror("nohup: stdout");
		close(output_fd);
		return -1;
	}
	close(output_fd);
	return 0;
}

int
nohup_main(int argc, char **argv)
{
	int priority;

	if (argc < 2) {
		fputs("usage: nohup command [ arguments ]\n", stderr);
		return 1;
	}
	if (signal(SIGHUP, SIG_IGN) == SIG_ERR ||
	    signal(SIGTERM, SIG_IGN) == SIG_ERR) {
		perror("nohup: signal");
		return 1;
	}
	if (redirect_terminal_output() < 0)
		return 1;
	if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
		perror("nohup: stderr");
		return 1;
	}
	errno = 0;
	priority = getpriority(PRIO_PROCESS, 0);
	if (errno != 0) {
		perror("nohup: priority");
		return 1;
	}
	if (setpriority(PRIO_PROCESS, 0, priority + 5) < 0) {
		perror("nohup: priority");
		return 1;
	}
	execvp(argv[1], &argv[1]);
	perror(argv[1]);
	return 1;
}
