/*
 * Host stand-in for <sys/uio.h>: the kernel's header includes its own
 * types.h, which the host's sys/types.h would conflict with. The layout
 * matches sys/sys/uio.h.
 */
#ifndef _SHIM_SYS_UIO_H_
#define _SHIM_SYS_UIO_H_

#include <stddef.h>
#include <sys/types.h>

/*
 * macOS declares struct iovec (void *iov_base) behind _STRUCT_IOVEC from
 * headers the test also includes; whichever comes first defines it, and the
 * test steps iov_base through a char pointer either way.
 */
#ifndef _STRUCT_IOVEC
#define _STRUCT_IOVEC
struct iovec {
	char		*iov_base;
	size_t		iov_len;
};
#endif

enum uio_rw { UIO_READ, UIO_WRITE };

struct uio {
	struct iovec	*uio_iov;
	int		uio_iovcnt;
	off_t		uio_offset;
	u_int		uio_resid;
	enum uio_rw	uio_rw;
};

#endif
