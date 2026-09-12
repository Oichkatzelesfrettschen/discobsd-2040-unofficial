#include <unistd.h>
#include <string.h>

static const char msg[] = "Hello, World!\n";

int
main(int argc, char **argv)
{
	write(1, msg, strlen(msg));
	write(1, argv[0], strlen(argv[0]));
	write(1, "\n", 1);
	return 0;
}
