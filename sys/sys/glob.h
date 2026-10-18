#ifndef _GLOB_H
#define _GLOB_H

#ifdef KERNEL
#include <sys/syscall_dispatch.h>
#else
extern int rdglob(int);
extern int wrglob(int,int);
#endif

#endif
