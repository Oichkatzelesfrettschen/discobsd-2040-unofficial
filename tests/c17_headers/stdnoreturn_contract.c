#include <stdnoreturn.h>

noreturn void stop_probe(void);
int returns_probe(int);

/*
 * The function has no return statement on the path that calls stop_probe.
 * -Wreturn-type accepts that only while the specifier reaches the
 * declaration, so the translation itself is the measurement.
 */
int
returns_probe(int value)
{
	if (value != 0)
		return value;
	stop_probe();
}
