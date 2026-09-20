#include <errno.h>
#include <paths.h>
#include <unistd.h>

long
db_sysconf(int name)
{
	(void)name;
	return 0;
}

size_t
db_confstr(int name, char *buffer, size_t length)
{
	static const char path[] = _PATH_STDPATH;
	size_t index;

	(void)name;
	if (buffer != NULL)
		for (index = 0; index < length && index < sizeof(path); index++)
			buffer[index] = path[index];
	return sizeof(path);
}
