#ifdef __cplusplus
extern "C" {
#endif
void __discobsd_assert_fail(const char *, const char *, int, const char *);
#ifdef __cplusplus
}
#endif

#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    __discobsd_assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

/*
 * C17 7.2p3: static_assert is defined whatever NDEBUG says.  C++ has the
 * keyword, so the macro stays out of a C++ translation unit.
 */
#ifndef __cplusplus
#undef static_assert
#define static_assert _Static_assert
#endif
