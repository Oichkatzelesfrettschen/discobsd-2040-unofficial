/* Compile-only checks for no-argument interfaces in the target unistd.h. */
#include <unistd.h>

int
main(void)
{
	pid_t (*getpgrp_function)(pid_t) = getpgrp;
	off_t (*lseek_function)(int, off_t, int) = lseek;
	ssize_t (*read_function)(int, void *, size_t) = read;
	unsigned int (*sleep_function)(unsigned int) = sleep;
	char *(*ttyname_function)(int) = ttyname;
	char *(*crypt_function)(char *, char *) = crypt;
	char *(*getpass_function)(char *) = getpass;
	char *(*getwd_function)(char *) = getwd;
	void (*psignal_function)(unsigned int, char *) = psignal;
	char *(*re_comp_function)(char *) = re_comp;
	u_long (*sethostid_function)(u_long) = sethostid;
	unsigned int (*ualarm_function)(unsigned int, unsigned int) = ualarm;
	void (*usleep_function)(long) = usleep;
	void (*sync_function)(void) = sync;

	(void)getpgrp_function;
	(void)lseek_function;
	(void)read_function;
	(void)sleep_function;
	(void)ttyname_function;
	(void)crypt_function;
	(void)getpass_function;
	(void)getwd_function;
	(void)psignal_function;
	(void)re_comp_function;
	(void)sethostid_function;
	(void)ualarm_function;
	(void)usleep_function;
	(void)sync_function;
	sync();
	(void)fork();
	(void)getegid();
	(void)geteuid();
	(void)getgid();
	(void)getlogin();
	(void)getpid();
	(void)getppid();
	(void)getuid();
	(void)getusershell();
	endusershell();
	(void)gethostid();
	setusershell();
	(void)vfork();
	return (0);
}
