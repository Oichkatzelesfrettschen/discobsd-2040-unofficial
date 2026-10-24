/*
 * Host and ILP32 contract gate for /dev/mem complete-span admission.
 *
 * The gate links the exact sys/dev/mem.c and includes the two RP2040
 * kernel-address functions extracted from machdep.c. Logical addresses let
 * the gate exercise interval crossings and 32-bit wrap without dereferencing
 * those addresses. The uiomove stub records only transfers admitted by mmrw.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/uio.h>

#include <machine/usercopy.h>

#define KERNEL_FLASH_LOWER UINT32_C(0x10000000)
#define KERNEL_FLASH_UPPER UINT32_C(0x10020000)
#define KERNEL_DATA_LOWER  UINT32_C(0x20000000)
#define KERNEL_DATA_UPPER  UINT32_C(0x20020000)
#define USER_LOWER         UINT32_C(0x20040000)
#define USER_UPPER         UINT32_C(0x20064000)

#ifndef MEM_KERNEL_SPAN_FUNCTIONS
#define MEM_KERNEL_SPAN_FUNCTIONS "mem_kernel_span_functions.c"
#endif

struct user u;

static caddr_t
logical_address(u_int value)
{
	return (caddr_t)(u_long)value;
}

#define __kernel_flash_start logical_address(KERNEL_FLASH_LOWER)
#define __kernel_flash_end logical_address(KERNEL_FLASH_UPPER)
#define __kernel_data_start logical_address(KERNEL_DATA_LOWER)
#define __kernel_data_end logical_address(KERNEL_DATA_UPPER)
#include MEM_KERNEL_SPAN_FUNCTIONS
#undef __kernel_flash_start
#undef __kernel_flash_end
#undef __kernel_data_start
#undef __kernel_data_end

static unsigned int uiomove_calls;
static caddr_t uiomove_address;
static u_int uiomove_bytes;
static int uiomove_error;

int
baduaddr_span(caddr_t address, u_int nbytes)
{
	u_int start = (u_int)(u_long)address;

	if (start < USER_LOWER || start > USER_UPPER)
		return 1;
	return nbytes > USER_UPPER - start;
}

int
baduaddr(caddr_t address)
{
	return baduaddr_span(address, 1);
}

int
uiomove(caddr_t address, u_int nbytes, struct uio *transfer)
{
	struct iovec *vector;

	uiomove_calls++;
	uiomove_address = address;
	uiomove_bytes = nbytes;
	if (uiomove_error != 0)
		return uiomove_error;
	vector = transfer->uio_iov;
	vector->iov_base += nbytes;
	vector->iov_len -= nbytes;
	transfer->uio_offset += nbytes;
	transfer->uio_resid -= nbytes;
	return 0;
}

static struct iovec vector;
static struct uio transfer;
static char buffer[32];

static void
reset(u_int address, u_int nbytes, enum uio_rw direction)
{
	u_int index;

	for (index = 0; index < sizeof(buffer); index++)
		buffer[index] = (char)(index + 1);
	vector.iov_base = buffer;
	vector.iov_len = nbytes;
	transfer.uio_iov = &vector;
	transfer.uio_iovcnt = 1;
	transfer.uio_offset = (off_t)address;
	transfer.uio_resid = nbytes;
	transfer.uio_rw = direction;
	uiomove_calls = 0;
	uiomove_address = NULL;
	uiomove_bytes = 0;
	uiomove_error = 0;
}

static void
check_kernel_span_service(void)
{
	int (*span_function)(caddr_t, u_int) = badkaddr_span;
	int (*byte_function)(caddr_t) = badkaddr;

	HK_CHECK(span_function != NULL);
	HK_CHECK(byte_function != NULL);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_FLASH_LOWER),
	    KERNEL_FLASH_UPPER - KERNEL_FLASH_LOWER) == 0);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_DATA_LOWER),
	    KERNEL_DATA_UPPER - KERNEL_DATA_LOWER) == 0);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_FLASH_UPPER), 0) == 0);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_DATA_UPPER), 0) == 0);
	HK_CHECK(badkaddr(logical_address(KERNEL_FLASH_UPPER - 1)) == 0);
	HK_CHECK(badkaddr(logical_address(KERNEL_FLASH_UPPER)) == 1);
	HK_CHECK(badkaddr(logical_address(KERNEL_DATA_UPPER - 1)) == 0);
	HK_CHECK(badkaddr(logical_address(KERNEL_DATA_UPPER)) == 1);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_FLASH_UPPER - 1), 2) == 1);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_DATA_UPPER - 1), 2) == 1);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_FLASH_UPPER - 1),
	    KERNEL_DATA_LOWER - KERNEL_FLASH_UPPER + 2) == 1);
	HK_CHECK(badkaddr_span(logical_address(KERNEL_DATA_LOWER),
	    UINT32_C(0xf0000001)) == 1);
}

static void
complete_spans_reach_uiomove(void)
{
	reset(USER_LOWER + 8, 8, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 0), &transfer, 0) == 0);
	HK_CHECK(uiomove_calls == 1);
	HK_CHECK((u_int)(u_long)uiomove_address == USER_LOWER + 8);
	HK_CHECK(uiomove_bytes == 8);

	reset(KERNEL_DATA_LOWER + 8, 8, UIO_WRITE);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 1), &transfer, 0) == 0);
	HK_CHECK(uiomove_calls == 1);

	reset(KERNEL_FLASH_UPPER - 8, 8, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 0), &transfer, 0) == 0);
	HK_CHECK(uiomove_calls == 1);
}

static void
crossing_and_wrapped_spans_are_rejected(void)
{
	u_int crossing_length;
	int discontiguous_error;
	int wrapped_error;

	crossing_length = KERNEL_DATA_LOWER - (KERNEL_FLASH_UPPER - 1) + 1;
	reset(KERNEL_FLASH_UPPER - 1, crossing_length, UIO_READ);
	discontiguous_error = mmrw(makedev(MEM_MAJOR, 0), &transfer, 0);
	HK_CHECK(discontiguous_error == EFAULT);
	HK_CHECK(uiomove_calls == 0);
	HK_CHECK(transfer.uio_resid == crossing_length);

	reset(KERNEL_DATA_LOWER, UINT32_C(0xf0000001), UIO_READ);
	wrapped_error = mmrw(makedev(MEM_MAJOR, 0), &transfer, 0);
	HK_CHECK(wrapped_error == EFAULT);
	HK_CHECK(uiomove_calls == 0);

	reset(USER_UPPER - 1, 2, UIO_WRITE);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 1), &transfer, 0) == EFAULT);
	HK_CHECK(uiomove_calls == 0);

	reset(KERNEL_DATA_UPPER, 1, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 0), &transfer, 0) == EFAULT);
	HK_CHECK(uiomove_calls == 0);
}

static void
provider_error_preserves_transfer(void)
{
	reset(KERNEL_DATA_LOWER, 4, UIO_READ);
	uiomove_error = EIO;
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 0), &transfer, 0) == EIO);
	HK_CHECK(uiomove_calls == 1);
	HK_CHECK(vector.iov_base == buffer);
	HK_CHECK(vector.iov_len == 4);
	HK_CHECK(transfer.uio_offset == (off_t)KERNEL_DATA_LOWER);
	HK_CHECK(transfer.uio_resid == 4);
}

static void
other_minors_preserve_behavior(void)
{
	u_int index;

	reset(7, 4, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 2), &transfer, 0) == 0);
	HK_CHECK(vector.iov_len == 4);
	HK_CHECK(transfer.uio_resid == 4);

	reset(7, 4, UIO_WRITE);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 2), &transfer, 0) == 0);
	HK_CHECK(vector.iov_len == 0);
	HK_CHECK(transfer.uio_resid == 0);
	HK_CHECK(transfer.uio_offset == 11);

	reset(7, 4, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 3), &transfer, 0) == 0);
	for (index = 0; index < 4; index++)
		HK_CHECK(buffer[index] == 0);
	HK_CHECK(vector.iov_len == 0);
	HK_CHECK(transfer.uio_resid == 0);

	reset(7, 4, UIO_WRITE);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 3), &transfer, 0) == EIO);
	HK_CHECK(vector.iov_len == 4);
	HK_CHECK(transfer.uio_resid == 4);

	reset(7, 4, UIO_READ);
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 4), &transfer, 0) == EINVAL);
	HK_CHECK(vector.iov_len == 4);
	HK_CHECK(transfer.uio_resid == 4);
}

static void
empty_iovecs_are_skipped(void)
{
	struct iovec vectors[2];

	reset(KERNEL_DATA_LOWER, 4, UIO_READ);
	vectors[0] = vector;
	vectors[0].iov_len = 0;
	vectors[1] = vector;
	transfer.uio_iov = vectors;
	transfer.uio_iovcnt = 2;
	HK_CHECK(mmrw(makedev(MEM_MAJOR, 0), &transfer, 0) == 0);
	HK_CHECK(uiomove_calls == 1);
	HK_CHECK(transfer.uio_iov == &vectors[1]);
	HK_CHECK(transfer.uio_iovcnt == 1);
}

int
main(void)
{
	check_kernel_span_service();
	complete_spans_reach_uiomove();
	crossing_and_wrapped_spans_are_rejected();
	provider_error_preserves_transfer();
	other_minors_preserve_behavior();
	empty_iovecs_are_skipped();
	return hk_verdict("memory-device span contracts");
}
