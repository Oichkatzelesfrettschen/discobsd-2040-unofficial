#ifndef _SHIM_SYS_BUF_H_
#define _SHIM_SYS_BUF_H_
#include <sys/types.h>

#define B_WRITE 0
#define B_READ 1
#define B_SWAPIMAGE 0x10000

struct buf {
    caddr_t b_addr;
};

struct buf *geteblk(void);
void brelse(struct buf *);
#endif
