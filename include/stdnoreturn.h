#ifndef _STDNORETURN_H_
#define _STDNORETURN_H_

/*
 * C17 7.23: noreturn names the function specifier.  C++ spells the same
 * property as an attribute, so the macro stays out of a C++ translation
 * unit.
 */
#ifndef __cplusplus
#define noreturn	_Noreturn
#endif

#endif /* !_STDNORETURN_H_ */
