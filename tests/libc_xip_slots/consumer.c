typedef unsigned (*operation)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);

#define KEEP __attribute__((used, noinline, externally_visible))

unsigned api_sum(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
operation peer_saved = api_sum;

KEEP operation
peer_address(void)
{
    return api_sum;
}

KEEP unsigned
peer_call(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
    return api_sum(a, b, c, d, e, f) + 2;
}
