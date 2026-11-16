#include <stdio.h>
#include <time.h>

double db_difftime(time_t end, time_t beginning);

struct difftime_case {
	time_t end;
	time_t beginning;
	double expected;
};

/*
 * time_t is the 32-bit view's unsigned count (sys/sys/_time_t.h), so an
 * earlier end gives a negative difference, and the carry out of bit 31 in
 * 2038 is one second like any other.
 */
static const struct difftime_case cases[] = {
	{ 0, 0, 0.0 },
	{ 1, 0, 1.0 },
	{ 0, 1, -1.0 },
	{ (time_t)0xffffffffUL, 0, 4294967295.0 },
	{ 0, (time_t)0xffffffffUL, -4294967295.0 },
	{ (time_t)0x80000000UL, (time_t)0x7fffffffUL, 1.0 },
	{ (time_t)0x7fffffffUL, (time_t)0x80000000UL, -1.0 },
	{ (time_t)0x90000000UL, (time_t)0x7fffffffUL, 268435457.0 },
	{ (time_t)0x7fffffffUL, (time_t)0xfffffffeUL, -2147483647.0 },
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
