/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Original Author: Shay Gal-on
*/

/* File: core_portme.c
   DiscoBSD port. Builds unchanged for the host (validation, this file's
   acceptance run) and for the rp2040 arm-none-eabi-gcc userland target --
   both supply gettimeofday(2), <sys/time.h>'s struct timeval, and printf(3)
   from the same libc lineage, so no #ifdef HOSTBUILD is needed here.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "coremark.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

/* Porting: Timing functions
        DiscoBSD has no clock(3)/CLOCKS_PER_SEC (checked: include/time.h
   declares no clock_t and lib/libc has no clock() member) and times(2)
   (lib/libc/compat/times.c) reports in a fixed 1/60 s tick regardless of the
   port's HZ, too coarse for a benchmark meant to resolve well under the
   10-second floor. gettimeofday(2) (include/sys/time.h, backed by a real
   syscall on every DiscoBSD port including rp2040) gives microsecond
   resolution and is available identically on the host build used to
   validate this port before it ever touches hardware.
*/
#define NSECS_PER_SEC     1000000UL
#define CORETIMETYPE      struct timeval
#define GETMYTIME(_t)     gettimeofday(_t, NULL)
#define MYTIMEDIFF(fin, ini)                                     \
    ((ee_u32)(((fin).tv_sec - (ini).tv_sec) * NSECS_PER_SEC      \
              + ((fin).tv_usec - (ini).tv_usec)))
#define TIMER_RES_DIVIDER 1
#define EE_TICKS_PER_SEC  (NSECS_PER_SEC / TIMER_RES_DIVIDER)

/** Define Host specific (POSIX), or target specific global time variables. */
static CORETIMETYPE start_time_val, stop_time_val;

/* Function: start_time
        This function will be called right before starting the timed portion
   of the benchmark.
*/
void
start_time(void)
{
    GETMYTIME(&start_time_val);
}
/* Function: stop_time
        This function will be called right after ending the timed portion of
   the benchmark.
*/
void
stop_time(void)
{
    GETMYTIME(&stop_time_val);
}
/* Function: get_time
        Return an abstract "ticks" number that signifies time on the system.

        Here that number is a plain microsecond count between start_time()
   and stop_time(), which coremark.h's time_in_secs() (upstream, not part of
   this port) divides by EE_TICKS_PER_SEC to get seconds.
*/
CORE_TICKS
get_time(void)
{
    CORE_TICKS elapsed
        = (CORE_TICKS)(MYTIMEDIFF(stop_time_val, start_time_val));
    return elapsed;
}
/* Function: time_in_secs
        Convert the value returned by get_time to seconds.
*/
secs_ret
time_in_secs(CORE_TICKS ticks)
{
    secs_ret retval = ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
    return retval;
}

ee_u32 default_num_contexts = 1;

/* Function: portable_init
        Target specific initialization code.
        Test for some common mistakes.
*/
void
portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; /* MAIN_HAS_NOARGC is 0, but this port takes no flags yet */
    (void)argv;

    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *))
    {
        ee_printf(
            "ERROR! Please define ee_ptr_int to a type that holds a "
            "pointer!\n");
    }
    if (sizeof(ee_u32) != 4)
    {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;
}
/* Function: portable_fini
        Target specific final code.
*/
void
portable_fini(core_portable *p)
{
    p->portable_id = 0;
}
