#include <limits.h>
#include <stdio.h>
#include <time.h>

double db_difftime(time_t end, time_t beginning);

struct difftime_case {
	time_t end;
	time_t beginning;
	double expected;
};

static const struct difftime_case cases[] = {
	{ 0, 0, 0.0 },
	{ 1, 0, 1.0 },
	{ 0, 1, -1.0 },
	{ (time_t)INT_MAX, (time_t)INT_MIN, 4294967295.0 },
	{ (time_t)INT_MIN, (time_t)INT_MAX, -4294967295.0 },
	{ (time_t)INT_MAX, -1, 2147483648.0 },
	{ -1, (time_t)INT_MAX, -2147483648.0 },
};

int
main(void)
{
	size_t case_index;
	int failures = 0;

	for (case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]);
	    case_index++) {
		double result = db_difftime(cases[case_index].end,
		    cases[case_index].beginning);

		if (result != cases[case_index].expected) {
			fprintf(stderr, "difftime case %lu: %.0f != %.0f\n",
			    (unsigned long)case_index, result,
			    cases[case_index].expected);
			failures++;
		}
	}
	if (failures != 0)
		return 1;
	puts("difftime contract passed");
	return 0;
}
