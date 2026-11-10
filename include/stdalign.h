#ifndef _STDALIGN_H_
#define _STDALIGN_H_

/*
 * C17 7.15: alignas and alignof name the keywords, and the two feature
 * macros are suitable for #if.  C++ has both as keywords and defines the
 * feature macros alone.
 */
#ifndef __cplusplus
#define alignas			_Alignas
#define alignof			_Alignof
#endif

#define __alignas_is_defined	1
#define __alignof_is_defined	1

#endif /* !_STDALIGN_H_ */
