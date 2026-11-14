/*
 * The calendar conversion ctime.c and mktime.c share.
 *
 * __discobsd_offtime() reads the instant at *timestamp, moved offset seconds
 * east, into every field of *result, including tm_gmtoff and an empty
 * tm_zone, and returns result. gmtime() and localtime() call it on the object they return;
 * timegm() calls it on the caller's own struct, which leaves that object
 * as the last gmtime() or localtime() call wrote it.
 */
#ifndef _LIBC_GEN_OFFTIME_H_
#define _LIBC_GEN_OFFTIME_H_

#include <time.h>

#ifndef __TIME64_VIEW
struct tm *__discobsd_offtime(const time_t *timestamp, long offset,
	    struct tm *result);
#else
/*
 * The 64-bit build (ctime.c and mktime.c compiled with _TIME_BITS defined
 * as 64) has its own conversion under its own symbol, so each width's
 * callers reach the one that reads their time_t.
 */
struct tm *__discobsd_offtime(const time_t *timestamp, long offset,
	    struct tm *result) __asm__("__discobsd_offtime64");

/*
 * Its range is every instant whose year an int tm_year holds: from
 * 1 January of year INT_MIN + 1900 (-2147481748) at 00:00:00 to
 * 31 December of year INT_MAX + 1900 (2147485547) at 23:59:59 UTC. Outside
 * it __discobsd_offtime() answers a null pointer with errno EOVERFLOW, and
 * timegm() and mktime() answer (time_t)-1.
 */
#define CAL64_MIN		(-67768040609740800LL)
#define CAL64_MAX		67768036191676799LL

/*
 * The Gregorian calendar repeats every 400 years (an era) of 146097 days,
 * a whole number of weeks. Counting each year from 1 March puts the leap
 * day last, so within an era every quantity is a closed form in 32 bits.
 * Both directions count eras from 1 March of the year CAL64_BIAS_ERAS eras
 * before year 0, where every count in the range is non-negative and floored
 * division is unsigned division; CAL64_BIAS_DAYS is the count of days from
 * there to 1 January 1970, of which CAL64_EPOCH_DAYS lie after 1 March of
 * year 0. The bias exceeds the 5,816,097 eras that any int tm_year with a
 * tm_mon folded into it reaches below year 0, and the biased era count
 * times 189 stays below 2^32.
 *
 * An era's days are taken as the two factors of 146097, 189 and 773, each
 * below 2^16: Thumb-1 has no divide or 64-bit multiply instruction and libc
 * carries none of the compiler's 64-bit helpers (check-imports), so a
 * 64-bit quotient or product is formed from 32-bit steps by a factor of
 * that size.
 */
#define CAL64_ERA_YEARS		400UL
#define CAL64_ERA_FACTOR_LOW	189UL
#define CAL64_ERA_FACTOR_HIGH	773UL
#define CAL64_BIAS_ERAS		5900000UL
#define CAL64_EPOCH_DAYS	719468UL
#define CAL64_BIAS_DAYS		((unsigned long long)CAL64_BIAS_ERAS * \
				    CAL64_ERA_FACTOR_LOW * CAL64_ERA_FACTOR_HIGH + \
				    CAL64_EPOCH_DAYS)
#endif

#endif /* !_LIBC_GEN_OFFTIME_H_ */
