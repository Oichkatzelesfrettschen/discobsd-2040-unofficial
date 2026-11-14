#include <stdint.h>

typedef uint32_t sigset_t;
#define NSIG 32
#define sigmask(signo) (1UL << ((signo) - 1))
