#include <sys/config.h>

static void
attach_service(void)
{
}

struct conf_service service = { attach_service };

#ifdef TEST_WRONG_ARITY
static void
attach_with_argument(int unit)
{
    (void)unit;
}

struct conf_service invalid_service = { attach_with_argument };
#endif
