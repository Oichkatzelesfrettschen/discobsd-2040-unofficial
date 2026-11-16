#include <stdio.h>
#include <time.h>

/*
 * The calendar entry points a date-printing program compiled on the board
 * links from /usr/lib/libc.a: the clock, the zone setup, both conversions
 * to fields and both conversions to text.
 */
int
main(void)
{
    time_t now = time((time_t *)0);
    int count;

    tzset();
    count = fputs(ctime(&now), stdout);
    count += fputs(asctime(gmtime(&now)), stdout);
    count += fputs(asctime(localtime(&now)), stdout);
    count += fputs(timezone(0, 0), stdout);
    return count < 0;
}
