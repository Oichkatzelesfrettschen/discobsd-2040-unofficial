#ifndef _STDDEF_H_
#define _STDDEF_H_

typedef int ptrdiff_t;

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned size_t;
#endif

#ifndef NULL
#define NULL    0
#endif

/* Offset of member MEMBER in a struct of type TYPE. */
#if defined(__GNUC__) && __GNUC__ > 3
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE*)0)->MEMBER)
#endif

/*
 * C17 7.19p2: an object type whose alignment is the greatest fundamental
 * alignment.  The members name every scalar class, so the union takes the
 * strictest alignment the selected ABI gives any of them.  Smaller C, the
 * native compiler, has no long long and does not see the type.
 */
#ifndef __SMALLER_C__
typedef union {
	long long	__max_align_integer;
	long double	__max_align_floating;
	void		*__max_align_object;
	void		(*__max_align_function)(void);
} max_align_t;
#endif

#endif /* _STDDEF_H_ */
