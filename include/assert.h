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
