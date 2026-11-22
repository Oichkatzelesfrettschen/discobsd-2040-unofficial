#ifndef _DEBUG_H
#define _DEBUG_H

#define DEBUG(...)
#define DEBUG1(...)
#define DEBUG2(...)
#define DEBUG3(...)
#define DEBUG4(...)
#define DEBUG5(...)
#define DEBUG6(...)
#define DEBUG7(...)
#define DEBUG8(...)
#define DEBUG9(...)

#ifdef GLOBAL_DEBUG
/*
 * With "options SYSTRACE" every level prints only while kern.systrace
 * holds SYSTRACE_EXEC (sys/systm.h), narrowed by kern.systracepid, so a
 * GLOBAL_DEBUG kernel stays quiet until sysctl kern.systrace=4. Without
 * SYSTRACE the lines print unconditionally.
 */
#ifdef SYSTRACE
#define DEBUG_PRINTF(...) \
	do { \
		if (systrace_enabled(SYSTRACE_EXEC)) \
			printf(__VA_ARGS__); \
	} while (0)
#else
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#endif

#undef DEBUG
#define DEBUG(...) DEBUG_PRINTF(__VA_ARGS__)

#if (GLOBAL_DEBUG>=1)
#undef DEBUG1
#define DEBUG1(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=2)
#undef DEBUG2
#define DEBUG2(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=3)
#undef DEBUG3
#define DEBUG3(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=4)
#undef DEBUG4
#define DEBUG4(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=5)
#undef DEBUG5
#define DEBUG5(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=6)
#undef DEBUG6
#define DEBUG6(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=7)
#undef DEBUG7
#define DEBUG7(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=8)
#undef DEBUG8
#define DEBUG8(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#if (GLOBAL_DEBUG>=9)
#undef DEBUG9
#define DEBUG9(...) DEBUG_PRINTF(__VA_ARGS__)
#endif

#endif /* GLOBAL_DEBUG */

#endif /* _DEBUG_H */
