/*
 * The kernel's buffer header, which needs only the types the shims give,
 * and the three buffer-cache entry points a driver calls (declared in the
 * header's KERNEL section, which the host does not compile).
 */
#include "../../../../sys/sys/buf.h"

struct buf	*geteblk(void);
void		brelse(struct buf *bp);
void		biodone(struct buf *bp);
