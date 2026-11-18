/*
 * Reboot ...
 *
 * Copyright (c) 1980,1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <pwd.h>
#include <sysexits.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syslog.h>
#include <sys/file.h>
#include <sys/reboot.h>
#include <sys/signal.h>

/*
 * -B enters the boot ROM's USB loader, as the name "bootloader" does. That
 * path ends in an unchanged flash image and a console that drops off the
 * bus, so it takes root and the explicit confirmation -y; the other modes
 * keep their historical behavior.
 */
#define	OPTS	"lqnhdarsfRBy"

static void markdown(void);

int
main(int argc, char **argv)
{
	int howto;		/* reboot options argument */
	int needlog = 1;	/* tell syslog what's happening */
	int quickly = 0;	/* go down quickly & ungracefully */
	int confirmed = 0;	/* -y: the caller armed the boot ROM loader */
	char *myname;		/* name we were invoked as */
	char args[20], *ap;	/* collected arguments for syslog */
	int i;

	if ((myname = rindex(argv[0], '/')) != NULL)
		myname++;
	else
		myname = argv[0];
	if (strcmp(myname, "halt") == 0)
		howto = RB_HALT;
	else if (strcmp(myname, "fasthalt") == 0)
		howto = RB_HALT|RB_NOFSCK;
	else if (strcmp(myname, "fastboot") == 0)
		howto = RB_NOFSCK;
	else if (strcmp(myname, "poweroff") == 0)
		howto = RB_HALT|RB_POWEROFF;
	else if (strcmp(myname, "bootloader") == 0)
		howto = RB_HALT|RB_BOOTLOADER;
	else
		howto = 0;

	ap = args;
	*ap++ = '-';
	*ap = '\0';
	while ((i = getopt(argc, argv, OPTS)) != EOF) {
		switch((char)i) {
			case 'l':  needlog = 0;		break;
			case 'q':  quickly++;		break;
			case 'n':  howto |= RB_NOSYNC;	break;
			case 'h':  howto |= RB_HALT;	break;
			case 'd':  howto |= RB_DUMP;	break;
			case 'a':  howto |= RB_ASKNAME;	break;
			case 'r':  howto |= RB_RDONLY;	break;
			case 's':  howto |= RB_SINGLE;	break;
			case 'f':  howto |= RB_NOFSCK;	break;
			case 'R':  howto |= RB_DFLTROOT; break;
			case 'p':  howto |= RB_HALT|RB_POWEROFF; break;
			case 'B':  howto |= RB_HALT|RB_BOOTLOADER; break;
			case 'y':  confirmed = 1;	break;
			case '?':
				fprintf(stderr,
					"usage: %s [-%s]\n", myname, OPTS);
				exit(EX_USAGE);
				/*NOTREACHED*/
		}
		if (index(args+1, (char)i) == 0) {
			*ap++ = (char)i;
			*ap = '\0';
		}
	}

	/*
	 * Refuse before syslog, sync or any signal: a refused request leaves
	 * the system exactly as it was.
	 */
	if (howto & RB_BOOTLOADER) {
		if (geteuid() != 0) {
			fprintf(stderr,
			    "%s: only root may enter the boot loader\n", myname);
			exit(EX_NOPERM);
		}
		if (!confirmed) {
			fprintf(stderr,
			    "%s: entering the boot loader detaches the console; "
			    "rerun with -y to confirm\n", myname);
			exit(EX_USAGE);
		}
	}
	if ((howto & (RB_NOSYNC|RB_NOFSCK)) == (RB_NOSYNC|RB_NOFSCK)
	    && !(howto & RB_HALT)) {
		fprintf(stderr,
			"%s: no sync and no fsck are a dangerous combination; no fsck ignored.\n",
			myname);
		howto &= ~RB_NOFSCK;
	}
	if (needlog) {
		char *user;
		struct passwd *pw;

		user = getlogin();
		if (user == (char *)0 && (pw = getpwuid(getuid())))
			user = pw->pw_name;
		if (user == (char *)0)
			user = "root";
		openlog(myname, 0, LOG_AUTH);
		syslog(LOG_CRIT, "%s; %s by %s",
 			args, (howto&RB_HALT)?"halted":"rebooted", user);
	}
        /*
         * Do a sync early on so disks start transfers while we're killing
         * processes.
         */
	if (! (howto & RB_NOSYNC))
		sync();

        printf("killing processes...");
        fflush(stdout);
	(void) signal(SIGHUP, SIG_IGN);	/* for remote connections */
	if (kill(1, SIGTSTP) == -1) {
		fprintf(stderr, "%s: can\'t idle init\n", myname);
		exit(EX_NOPERM);
	}
	sleep(1);
	(void) kill(-1, SIGTERM);	/* one chance to catch it */
	sleep(1);
        printf(" done\n");

        /*
         * After the processes receive the TERM signal start the rest of the
         * buffers out to disk.  Wait five seconds between SIGTERM and SIGKILL so
         * the processes have a chance to clean up and exit nicely.
         */
	if (!(howto & RB_NOSYNC)) {
		sync();
		sleep(1);
        }

	if (! quickly) {
		for (i = 1; ; i++) {
			if (kill(-1, SIGKILL) == -1) {
				if (errno == ESRCH)
					break;
				perror(myname);
				kill(1, SIGHUP);
				exit(EX_OSERR);
			}
			if (i > 5) {
				fprintf(stderr,
				    "CAUTION: some process(es) wouldn't die\n");
				break;
			}
			sleep(i);
		}
                if (! (howto & RB_NOSYNC)) {
                        markdown();
                        sync();
                        sleep(1);
                }
        }
	reboot(howto);
	perror(myname);
	kill(1, SIGHUP);
	exit(EX_OSERR);
}


/*
 * Make shutdown entry in /usr/adm/utmp.
 */
#include <utmp.h>
#include <paths.h>

#define	SCPYN(a, b)	strncpy(a, b, sizeof(a))

static void
markdown(void)
{
	struct utmp wtmp;
	register int f = open(_PATH_WTMP, O_WRONLY|O_APPEND);

	if (f >= 0) {
		bzero((char *)&wtmp, sizeof(wtmp));
		SCPYN(wtmp.ut_line, "~");
		SCPYN(wtmp.ut_name, "shutdown");
		SCPYN(wtmp.ut_host, "");
		wtmp.ut_time = (utime32_t)time((time_t *)0);
		write(f, (char *)&wtmp, sizeof(wtmp));
		close(f);
	}
}
