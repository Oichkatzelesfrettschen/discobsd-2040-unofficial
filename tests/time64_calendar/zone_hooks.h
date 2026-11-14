/*
 * The zone the calendar test selects. tzset.c is compiled with getenv()
 * and gettimeofday() renamed to the two hooks zone_hooks.c defines, so
 * zone_tz stands for the TZ variable (a null pointer when it is unset) and
 * zone_minuteswest for the offset the kernel reports. Plain integer and
 * pointer types keep one layout for the 64-bit test unit and the 32-bit
 * hook unit.
 */
#ifndef ZONE_HOOKS_H
#define ZONE_HOOKS_H

extern const char *zone_tz;
extern int zone_minuteswest;

#endif
