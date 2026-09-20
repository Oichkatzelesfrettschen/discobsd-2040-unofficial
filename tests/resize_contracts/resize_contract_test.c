#define TERMIOS 1
#define main resize_program_main
#include "../../usr.bin/resize/resize.c"
#undef main

#include <stdio.h>

struct reply_case {
	const char *reply;
	int valid;
	int rows;
	int cols;
};

int
main(void)
{
	static const struct reply_case cases[] = {
		{ "\033[1;1R", 1, 1, 1 },
		{ "\033[999;999R", 1, 999, 999 },
		{ "\033[024;080R", 1, 24, 80 },
		{ "\033[0;1R", 0, 0, 0 },
		{ "\033[1;0R", 0, 0, 0 },
		{ "\033[1000;1R", 0, 0, 0 },
		{ "\033[1;1000R", 0, 0, 0 },
		{ "\033[-1;2R", 0, 0, 0 },
		{ "\033[+1;2R", 0, 0, 0 },
		{ "\033[ 1;2R", 0, 0, 0 },
		{ "\033[1 ;2R", 0, 0, 0 },
		{ "\033[1;2 R", 0, 0, 0 },
		{ "\033[1:2R", 0, 0, 0 },
		{ "\033[1;2;3R", 0, 0, 0 },
		{ "\033[1;2", 0, 0, 0 },
		{ "\033[1;2RX", 0, 0, 0 },
		{ "\033[;2R", 0, 0, 0 },
		{ "\033[1;R", 0, 0, 0 },
		{ "1;2R", 0, 0, 0 },
		{ "", 0, 0, 0 }
	};
	unsigned int index;
	int failures = 0;

	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		int rows = -1;
		int cols = -1;
		int valid = parse_reply(cases[index].reply, &rows, &cols) == 0;

		if (valid != cases[index].valid ||
		    (valid && (rows != cases[index].rows ||
		    cols != cases[index].cols))) {
			fprintf(stderr, "resize contract case %u failed\n", index);
			failures++;
		}
	}
	if (failures == 0)
		printf("resize parser contract tests passed\n");
	return failures != 0;
}
