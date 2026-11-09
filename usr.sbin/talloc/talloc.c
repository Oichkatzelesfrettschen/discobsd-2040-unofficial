#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/swap.h>

void usage()
{
	printf("Usage: talloc /dev/tempX <size>\n");
}

/*
 * TFALLOC changes a temporary device's swap extent only for the superuser
 * through a descriptor open for writing (sys/dev/swap.c), and reports a
 * shortage by returning a size of zero.
 */
int main(int argc, char *argv[])
{
    int fd;
    off_t size, requested;

    if (argc != 3) {
        usage();
        return 10;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 10;
    }

    requested = size = atol(argv[2]);

    if (ioctl(fd, TFALLOC, &size) < 0) {
        perror(argv[1]);
        close(fd);
        return 10;
    }
    if (requested > 0 && size != requested) {
        printf("%s: could not allocate %ld blocks\n", argv[1],
            (long)requested);
        close(fd);
        return 10;
    }

    close(fd);

    return 0;
}
