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

/* File: core_portme.h
   DiscoBSD port (host and rp2040 arm-none-eabi-gcc userland). Timing runs
   through gettimeofday(2), memory comes from a stack array (MEM_STACK), and
   ee_printf is the tree's own printf(3) (HAS_PRINTF below). No malloc, no
   threads, no platform-specific headers beyond <sys/time.h>.
*/
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h> /* size_t, used by ee_size_t below */

/************************/
/* Data types and settings */
/************************/
/* Configuration: HAS_FLOAT
        Zero on the board: the score line and Iterations/Sec then print as
   integers (core_main.c's #if HAS_FLOAT else branch), which the run rules
   allow. HAS_FLOAT=1 routes those through printf %f, and doprnt's float
   formatter carries a large stack frame; on top of CoreMark's own deep
   workload stack it overflows the 96 KB process window and faults right
   after "Correct operation validated". Integer reporting avoids that and
   drops doprnt_float.o (no PRINTF_FLOAT=yes needed). The timing macros
   below (GETMYTIME/MYTIMEDIFF/CORE_TICKS) are pure integer microseconds.
   core_main.c's own integer report is coarse: time_in_secs() returns whole
   seconds, which it divides into the iteration count, so its Iterations/Sec
   truncates (2000 / 8 = 250 for an 8.254 s, 242.3/s run) and its canonical
   CoreMark 1.0 score line, guarded by HAS_FLOAT, is omitted. core_portme.c's
   portable_fini() recomputes the rate at microsecond resolution and prints
   the accurate value.
*/
#ifndef HAS_FLOAT
#define HAS_FLOAT 0
#endif
/* Configuration: HAS_TIME_H
        DiscoBSD's <time.h> exists but has no clock_t/clock(3); this port
   times through <sys/time.h>'s gettimeofday(2) instead, so this flag stays
   informational only.
*/
#ifndef HAS_TIME_H
#define HAS_TIME_H 1
#endif
/* Configuration: USE_CLOCK
        Off: the tree has no clock(3)/CLOCKS_PER_SEC. See core_portme.c for
   the gettimeofday(2)-based timer this port uses instead.
*/
#ifndef USE_CLOCK
#define USE_CLOCK 0
#endif
/* Configuration: HAS_STDIO / HAS_PRINTF
        DiscoBSD's libc supplies both; ee_printf maps to printf(3) via
   coremark.h's own "#if HAS_PRINTF / #define ee_printf printf".
*/
#ifndef HAS_STDIO
#define HAS_STDIO 1
#endif
#ifndef HAS_PRINTF
#define HAS_PRINTF 1
#endif

/* Definitions: COMPILER_VERSION, COMPILER_FLAGS, MEM_LOCATION
        Initialize these strings per platform.
*/
#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "Please put compiler version here (e.g. gcc 4.1)"
#endif
#endif
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS \
    FLAGS_STR /* passed on the command line by the Makefile */
#endif
#ifndef MEM_LOCATION
#define MEM_LOCATION "STACK"
#endif

/* Data Types:
        To avoid compiler issues, define the data types that need to be used
   for 8b, 16b and 32b in <core_portme.h>.

        *Important*:
        ee_ptr_int needs to be the data type used to hold pointers, otherwise
   coremark may fail!!!
*/
typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef double         ee_f32;
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
/* ee_ptr_int must hold a pointer without truncation (README's own warning).
   ee_u32 works on the rp2040 target (32b pointers) but truncates a 64b host
   pointer and segfaults align_mem() in core_matrix.c, so this port uses
   size_t here -- it matches the address width on both the rp2040 target and
   whatever host validates this port before hardware. */
typedef size_t         ee_ptr_int;
typedef size_t         ee_size_t;

/* Configuration: CORE_TICKS
        Return type of the timing functions. get_time()/MYTIMEDIFF() (in
   core_portme.c) fit a whole benchmark run's microsecond count in 32 bits
   for any run under about 4295 seconds (71 minutes), well past the 10-second
   floor and the few-minute runs this port targets.
*/
typedef ee_u32 CORE_TICKS;

/* align_mem:
        This macro is used to align an offset to point to a 32b value. It is
   used in the Matrix algorithm to initialize the input memory blocks.
*/
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

/* Configuration: SEED_METHOD
        Seeds come from the volatile globals in core_portme.c (no command
   line, no host RNG) -- SEED_VOLATILE matches the "simple"/"posix" upstream
   ports and needs nothing DiscoBSD-specific.
*/
#ifndef SEED_METHOD
#define SEED_METHOD SEED_VOLATILE
#endif

/* Configuration: MEM_METHOD
        MEM_STACK: the 2000-byte (TOTAL_DATA_SIZE) work buffer lives on
   core_main's C stack (stack_memblock[]) -- no malloc(3) dependency, and the
   buffer is well inside the rp2040 port's 96 KB user data window
   (USER_DATA_SIZE, sys/arch/rp2040/include/machparam.h) with enormous
   headroom.
*/
#ifndef MEM_METHOD
#define MEM_METHOD MEM_STACK
#endif

/* Configuration: MULTITHREAD
        DiscoBSD is single-core RP2040 userland here; one context.
*/
#ifndef MULTITHREAD
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0
#endif

/* Configuration: MAIN_HAS_NOARGC
        DiscoBSD's exec(2)/execve(2) pass argc/argv to main() normally.
*/
#ifndef MAIN_HAS_NOARGC
#define MAIN_HAS_NOARGC 0
#endif

/* Configuration: MAIN_HAS_NORETURN
        main() returns an int normally; init(8)/the shell collect it via
   wait(2), same as any other DiscoBSD program.
*/
#ifndef MAIN_HAS_NORETURN
#define MAIN_HAS_NORETURN 0
#endif

/* Variable: default_num_contexts
        Not used for this single-context port, must contain the value 1.
*/
extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

/* target specific init/fini */
void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) \
    && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

#endif /* CORE_PORTME_H */
