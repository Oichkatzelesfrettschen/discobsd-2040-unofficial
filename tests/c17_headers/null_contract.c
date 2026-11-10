/*
 * Compiled once per header, with NULL_HEADER naming it.  Each of the eight
 * headers defines NULL under #ifndef, so the one a translation unit
 * includes first decides the macro; including each alone shows that every
 * one of them gives a pointer constant.
 */
#ifdef NULL_NEEDS_TYPES
#include <sys/types.h>
#endif
#include NULL_HEADER

#ifndef NULL
#error "the header defines NULL"
#endif

_Static_assert(_Generic(NULL, void *: 1, default: 0),
    "NULL has type void *, so an integer use is a diagnosed conversion");
