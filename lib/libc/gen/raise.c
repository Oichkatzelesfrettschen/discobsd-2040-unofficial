#include <signal.h>
#include <unistd.h>

int
raise(int signal_number)
{
	return kill(getpid(), signal_number);
}
