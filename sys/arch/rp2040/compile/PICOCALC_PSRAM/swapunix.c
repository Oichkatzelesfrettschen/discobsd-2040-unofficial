#include <sys/param.h>
#include <sys/conf.h>

dev_t	rootdev = makedev(0, 1);	/* fl0a */
dev_t	dumpdev = NODEV;		/* none */
dev_t	swapdev = makedev(0, 8);	/* fl1` */
