/*
 * Known-bad input for check_imports.sh: a 64-bit division ARMv6-M cannot
 * open-code, so the object imports a runtime divide routine.
 */
unsigned long long divide_import_bad(unsigned long long, unsigned long long);

unsigned long long
divide_import_bad(unsigned long long dividend, unsigned long long divisor)
{
	return dividend / divisor;
}
