/*
 * Host-build shim for bin/sh.
 *
 * bin/sh is 2.11BSD Bourne shell source: K&R definitions, 32-bit
 * pointer-in-int assumptions, <sgtty.h> line discipline, BSD
 * union wait, and a raw getdirentries(2)-style directory scan in
 * expand.c.  posix-sh.sh force-includes this header into every
 * translation unit so the same sources compile against a modern
 * libc without editing them; hostshim.c supplies the three
 * functions that need a real implementation.
 *
 * Nothing here is compiled into the board binary: the rp2040 build
 * goes through bin/sh/Makefile, which never mentions this file.
 */
#ifndef SH_HOSTSHIM_H
#define SH_HOSTSHIM_H

/* fault.c arms SIGEMT, which Linux does not define. */
#define SIGEMT SIGSYS

/* service.c await() reads the wait status through the 4.2BSD union. */
union wait {
	int w_status;
	struct {
		unsigned int w_Termsig:7;
		unsigned int w_Coredump:1;
		unsigned int w_Retcode:8;
		unsigned int w_Filler:16;
	} w_T;
};
#define w_termsig	w_T.w_Termsig
#define w_coredump	w_T.w_Coredump
#define w_retcode	w_T.w_Retcode

/*
 * expand.c declares `static DIR dirbuf' and reads directory blocks
 * into dirbuf.dd_buf itself.  glibc's DIR is opaque, so define the
 * struct it is a typedef of before <dirent.h> is reached.  Only the
 * expand.c compile sets SH_HOST_RAWDIR, so opendir(3) keeps its own
 * layout everywhere else, including hostshim.c.
 */
#ifdef SH_HOST_RAWDIR
#define DIRBLKSIZ 4096
struct __dirstream {
	long dd_loc;
	long dd_size;
	char dd_buf[DIRBLKSIZ];
};
long sh_host_getdents();
#endif

#endif /* SH_HOSTSHIM_H */
