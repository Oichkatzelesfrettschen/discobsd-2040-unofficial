#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern const char *_curbrk;
extern const char *__progname;
extern int optional_object __attribute__((weak));
extern int absolute_zero __attribute__((weak));

int *library_errno_pointer = &errno;
FILE *library_stream_pointer = &_iob[2];
void *library_null_pointer;

int *shared_errno_address(void);
FILE *shared_stream_address(void);
char ***shared_environ_address(void);
int shared_initial_state(const char *);
int shared_callback(int, int (*)(int));

int *
shared_errno_address(void)
{
	return &errno;
}

FILE *
shared_stream_address(void)
{
	return _iob;
}

char ***
shared_environ_address(void)
{
	return &environ;
}

int
shared_initial_state(const char *end)
{
	return &optional_object == NULL && &absolute_zero == NULL &&
	    library_errno_pointer == &errno &&
	    library_stream_pointer == &_iob[2] && library_null_pointer == NULL &&
	    _iob[0]._flag == _IOREAD && _iob[0]._file == 0 &&
	    _iob[1]._flag == _IOWRT && _iob[1]._file == 1 &&
	    _iob[2]._flag == (_IOWRT | _IONBF) && _iob[2]._file == 2 &&
	    _curbrk == end && __progname[0] == '\0' && environ == NULL;
}

int
shared_callback(int value, int (*callback)(int))
{
	int result;

	errno = value;
	_iob[2]._cnt = value;
	result = callback(value + 1);
	return errno + _iob[2]._cnt + result;
}
