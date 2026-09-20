/*
 * login clears inherited local tty modes during authentication and restores
 * the exact saved word after authentication commits to a user session.
 */

#include <sys/ioctl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "tty_modes.h"

struct ioctl_record {
	unsigned int request;
	int value;
};

static struct ioctl_record records[3];
static int record_count;
static int driver_modes = LPASS8 | LCTLECH;

int
test_ioctl(int descriptor, int request, ...)
{
	va_list arguments;
	int *value;

	va_start(arguments, request);
	value = va_arg(arguments, int *);
	va_end(arguments);
	(void)descriptor;
	if (record_count < 3) {
		records[record_count].request = (unsigned int)request;
		records[record_count].value =
		    (unsigned int)request == (unsigned int)TIOCLGET ?
		    driver_modes : *value;
	}
	record_count++;
	if ((unsigned int)request == (unsigned int)TIOCLGET)
		*value = driver_modes;
	else if ((unsigned int)request == (unsigned int)TIOCLSET)
		driver_modes = *value;
	return 0;
}

static int
fail(const char *message)
{
	(void)write(2, message, strlen(message));
	(void)write(2, "\n", 1);
	return 1;
}

int
main(void)
{
	const int inherited_modes = driver_modes;
	int saved_modes;

	login_local_modes_clear(0, &saved_modes);
	if (record_count != 2 ||
	    records[0].request != (unsigned int)TIOCLGET ||
	    records[1].request != (unsigned int)TIOCLSET ||
	    records[1].value != 0 || saved_modes != inherited_modes ||
	    driver_modes != 0)
		return fail("login tty modes: save and clear sequence differs");

	login_local_modes_restore(0, saved_modes);
	if (record_count != 3 ||
	    records[2].request != (unsigned int)TIOCLSET ||
	    records[2].value != inherited_modes ||
	    driver_modes != inherited_modes)
		return fail("login tty modes: inherited local modes are not restored");

	(void)write(1, "login tty modes: pass\n", 22);
	return 0;
}
