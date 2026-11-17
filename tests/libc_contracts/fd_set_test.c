#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syslimits.h>

char *autobaud(void);

static int stage, failed, ready, received, byte_value;

int
probe_ioctl(int fd, int request, ...)
{
	va_list arguments;
	int *value;

	if (fd != 0 || request != (int)TIOCFLUSH ||
	    (stage != 0 && stage != 4)) {
		failed = 1;
		return -1;
	}
	va_start(arguments, request);
	value = va_arg(arguments, int *);
	va_end(arguments);
	if (value == 0 || *value != 0)
		failed = 1;
	stage++;
	return 0;
}

int
probe_select(int nfds, fd_set *readfds, fd_set *writefds,
    fd_set *errorfds, struct timeval *timeout)
{
	if (nfds != 32 || writefds != 0 || errorfds != 0)
		failed = 1;
	if (stage == 1) {
		if (readfds == 0 || readfds->fds_bits[0] != 1 ||
		    timeout->tv_sec != 30 || timeout->tv_usec != 0)
			failed = 1;
		stage++;
		return ready;
	}
	if (stage != 3 || readfds != 0 || timeout->tv_sec != 0 ||
	    timeout->tv_usec != 20)
		failed = 1;
	stage++;
	return -1;
}

ssize_t
probe_read(int fd, void *buffer, size_t length)
{
	if (stage != 2 || fd != 0 || length != 1)
		failed = 1;
	stage++;
	if (received == 1)
		*(unsigned char *)buffer = (unsigned char)byte_value;
	return received;
}

static int
same_string(const char *left, const char *right)
{
	while (*left && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static int
set_cases(void)
{
	struct guarded_set {
		unsigned long before;
		fd_set set;
		unsigned long after;
	} guarded;
	int descriptor, other;

	guarded.before = 0x12345678UL;
	guarded.after = 0x87654321UL;
	guarded.set.fds_bits[0] = -1;
	if (1)
		FD_ZERO(&guarded.set);
	else
		return 1;
	if (guarded.set.fds_bits[0] != 0 || guarded.before != 0x12345678UL ||
	    guarded.after != 0x87654321UL)
		return 2;
	for (descriptor = 0; descriptor < OPEN_MAX; descriptor++) {
		FD_SET(descriptor, &guarded.set);
		FD_SET(descriptor, &guarded.set);
		for (other = 0; other < OPEN_MAX; other++)
			if (!!FD_ISSET(other, &guarded.set) != (other <= descriptor))
				return 3;
	}
	for (descriptor = 0; descriptor < OPEN_MAX; descriptor++) {
		FD_CLR(descriptor, &guarded.set);
		FD_CLR(descriptor, &guarded.set);
		for (other = 0; other < OPEN_MAX; other++)
			if (!!FD_ISSET(other, &guarded.set) != (other > descriptor))
				return 4;
	}
	guarded.set.fds_bits[0] = -1;
	FD_ZERO(&guarded.set);
	FD_ZERO(&guarded.set);
	return guarded.set.fds_bits[0] != 0 || guarded.before != 0x12345678UL ||
	    guarded.after != 0x87654321UL;
}

int
main(void)
{
	int value, readiness, count;
	const char *expected;

	if (set_cases())
		return 10;
	for (readiness = -1; readiness <= 1; readiness++) {
		for (count = -1; count <= 1; count++) {
			for (value = 0; value < 256; value++) {
				stage = failed = 0;
				ready = readiness;
				received = count;
				byte_value = value;
				expected = "1200-baud";
				if (ready == 1 && received == 1) {
					switch (value) {
					case 128: expected = "300-baud"; break;
					case 230: expected = "1200-baud"; break;
					case 13: case 141: expected = "2400-baud"; break;
					case 255: expected = "9600-baud"; break;
					default: expected = "4800-baud"; break;
					}
				}
				if (!same_string(autobaud(), expected) || failed)
					return 20;
				if (stage != (ready != 1 ? 2 : received != 1 ? 3 : 5))
					return 21;
			}
		}
	}
	return 0;
}
