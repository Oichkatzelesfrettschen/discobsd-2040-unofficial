#include <stdio.h>
#include <string.h>

int false_main(int, char **);
int nohup_main(int, char **);
int true_main(int, char **);

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fputs("driver requires an applet name\n", stderr);
		return 2;
	}
	argc--;
	argv++;
	if (strcmp(argv[0], "true") == 0)
		return true_main(argc, argv);
	if (strcmp(argv[0], "false") == 0)
		return false_main(argc, argv);
	if (strcmp(argv[0], "nohup") == 0)
		return nohup_main(argc, argv);
	fprintf(stderr, "unknown applet: %s\n", argv[0]);
	return 2;
}
