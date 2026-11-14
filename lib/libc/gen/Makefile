#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= ${DEFS} -Os

SRCS		= ${STDSRC} ${TIME64SRC}
OBJS		= ${STDOBJ} ${TIME64OBJ} ${WIDEOBJ}

# The system calls of the 64-bit view of time (sys/_time_t.h): each member
# defines one __NAME64 symbol over the 32-bit entry point, so a program
# links only the wrappers it calls. time64.h states the conversion rules.
TIME64SRC	= time64_widen_stat.c time64_widen_rusage.c time64_stat.c time64_lstat.c \
		  time64_fstat.c time64_gettimeofday.c time64_settimeofday.c time64_adjtime.c \
		  time64_select.c time64_pselect.c time64_utimes.c time64_futimes.c \
		  time64_getitimer.c time64_setitimer.c time64_getrusage.c time64_wait4.c
TIME64OBJ	= time64_widen_stat.o time64_widen_rusage.o time64_stat.o time64_lstat.o \
		  time64_fstat.o time64_gettimeofday.o time64_settimeofday.o time64_adjtime.o \
		  time64_select.o time64_pselect.o time64_utimes.o time64_futimes.o \
		  time64_getitimer.o time64_setitimer.o time64_getrusage.o time64_wait4.o

# The calendar functions of the 64-bit view, and wait3(): ctime.c,
# mktime.c, time.c, difftime.c and wait3.c compiled a second time with
# _TIME_BITS defined as 64, where <time.h> and <sys/wait.h> bind each public
# name to __NAME64 and offtime.h the shared conversion to
# __discobsd_offtime64. NAME64.o is the member built from NAME.c; the zone
# both widths read is tzset.o's.
WIDEOBJ		= ctime64.o mktime64.o time64.o difftime64.o wait364.o

STDSRC		= abort.c alarm.c aligned_alloc.c assert_fail.c asctime.c atof.c atoi.c atol.c \
		  basename.c bcmp.c bcopy.c ctermid.c \
		  bzero.c calloc.c closedir.c confstr.c crypt.c ctime.c ctype_.c \
		  daemon.c devname.c difftime.c dirname.c \
		  ecvt.c err.c execvp.c fakcu.c ffs.c \
		  fnmatch.c frexp.c fstab.c gcvt.c getenv.c getgrent.c getgrgid.c \
		  getgrnam.c getgrouplist.c gethostname.c getloadavg.c \
		  getlogin.c getmntinfo.c getpagesize.c getpass.c \
		  getcwd.c getpwent.c getttyent.c getttynam.c getusershell.c \
		  getwd.c index.c initgroups.c isascii.c isatty.c isblank.c \
		  isctype.c isinff.c isnanf.c \
		  knlist.c ldexp.c malloc.c mktemp.c mktime.c modf.c modff.c ndbm.c \
		  nlist.c opendir.c perror.c popen.c psignal.c qsort.c qsort_r.c raise.c \
		  random.c readdir.c regex.c rindex.c scandir.c seekdir.c \
		  setenv.c sethostname.c setmode.c setpgid.c siginterrupt.c \
		  siglist.c signal.c sigsetops.c sleep.c strcasecmp.c \
		  stpcpy.c stpncpy.c strcat.c strcmp.c strcpy.c strdup.c \
		  strndup.c strnlen.c strftime.c strlen.c \
		  strncat.c strncmp.c strncpy.c swab.c sysconf.c sysctl.c syslog.c \
		  system.c telldir.c time.c timezone.c tolower.c toupper.c tzset.c \
		  ttyname.c ttyslot.c \
		  ualarm.c uname.c unvis.c usleep.c vis.c \
		  wait.c wait3.c waitpid.c

STDOBJ		= abort.o alarm.o aligned_alloc.o assert_fail.o asctime.o atof.o atoi.o atol.o \
		  basename.o bcmp.o bcopy.o ctermid.o \
		  bzero.o calloc.o closedir.o confstr.o crypt.o ctime.o ctype_.o \
		  daemon.o devname.o difftime.o dirname.o \
		  ecvt.o err.o execvp.o fakcu.o ffs.o \
		  fnmatch.o frexp.o fstab.o gcvt.o getenv.o getgrent.o getgrgid.o \
		  getgrnam.o getgrouplist.o gethostname.o getloadavg.o \
		  getlogin.o getmntinfo.o getpagesize.o getpass.o \
		  getcwd.o getpwent.o getttyent.o getttynam.o getusershell.o \
		  getwd.o index.o initgroups.o isascii.o isatty.o isblank.o \
		  isctype.o isinff.o isnanf.o \
		  knlist.o ldexp.o malloc.o mktemp.o mktime.o modf.o modff.o ndbm.o \
		  nlist.o opendir.o perror.o popen.o psignal.o qsort.o qsort_r.o raise.o \
		  random.o readdir.o regex.o rindex.o scandir.o seekdir.o \
		  setenv.o sethostname.o setmode.o setpgid.o siginterrupt.o \
		  siglist.o signal.o sigsetops.o sleep.o strcasecmp.o \
		  stpcpy.o stpncpy.o strcat.o strcmp.o strcpy.o strdup.o \
		  strndup.o strnlen.o strftime.o strlen.o \
		  strncat.o strncmp.o strncpy.o swab.o sysconf.o sysctl.o syslog.o \
		  system.o telldir.o time.o timezone.o tolower.o toupper.o tzset.o \
		  ttyname.o ttyslot.o \
		  ualarm.o uname.o unvis.o usleep.o vis.o \
		  wait.o wait3.o waitpid.o

gen.a:		${OBJS}
		@echo "building gen.a"
		@${AR} cr gen.a ${OBJS}

qsort_r.o: qsort.c

.for member in ${WIDEOBJ:S/64.o$//}
${member}64.o: ${member}.c
		${CC} ${CFLAGS} -U_TIME_BITS -D_TIME_BITS=64 -c -o $@ ${.CURDIR}/${member}.c
.endfor

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

clean:
		rm -f gen.a *.o *~ profiled/*.o tags Makefile.bak

depend:
		for i in ${SRCS}; do \
		    cc -M ${CFLAGS} $$i | awk ' { if ($$1 != prev) \
			{ if (rec != "") print rec; rec = $$0; prev = $$1; } \
			else { if (length(rec $$2) > 78) { print rec; rec = $$0; } \
			else rec = rec " " $$2 } } \
			END { print rec } ' >> makedep; done
		echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
		echo '$$r makedep' >>eddep
		echo 'w' >>eddep
		cp Makefile Makefile.bak
		ed - Makefile < eddep
		rm eddep makedep
		echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile
		echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile
		echo '# see make depend above' >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
