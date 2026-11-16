typedef unsigned (*operation)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);

#define KEEP __attribute__((used, noinline, externally_visible))

unsigned counter = 7;
unsigned api_sum(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
unsigned api_alias(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);

KEEP unsigned
api_sum(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
    return a + 3 * b + 5 * c + 7 * d + 11 * e + 13 * f + counter++;
}

unsigned __libc_impl_api_sum(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned)
    __attribute__((alias("api_sum")));
unsigned api_alias(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned)
    __attribute__((weak, alias("api_sum")));
unsigned __libc_impl_api_alias(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned)
    __attribute__((alias("api_sum")));

#ifdef INITIALIZED_BYPASS
operation saved[] = { __libc_impl_api_sum, api_alias };
#else
operation saved[] = { api_sum, api_alias };
#endif

KEEP operation
api_address(void)
{
#ifdef RETURNED_BYPASS
    return __libc_impl_api_sum;
#else
    return api_sum;
#endif
}

KEEP operation
api_alias_address(void)
{
    return api_alias;
}

KEEP unsigned
api_nested(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
    return api_sum(a, b, c, d, e, f) + 1;
}

KEEP unsigned
api_through(operation callback, unsigned a, unsigned b, unsigned c,
    unsigned d, unsigned e, unsigned f)
{
    return callback(a, b, c, d, e, f) + 3;
}
