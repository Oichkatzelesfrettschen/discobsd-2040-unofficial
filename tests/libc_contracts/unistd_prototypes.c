/* Compile-only checks for no-argument interfaces in the target unistd.h. */
#include <unistd.h>

int
main(void)
{
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
