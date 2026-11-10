#include <stddef.h>

_Static_assert(_Alignof(max_align_t) >= _Alignof(long long),
    "max_align_t covers long long");
_Static_assert(_Alignof(max_align_t) >= _Alignof(long double),
    "max_align_t covers long double");
_Static_assert(_Alignof(max_align_t) >= _Alignof(void *),
    "max_align_t covers object pointers");
_Static_assert(_Alignof(max_align_t) >= _Alignof(void (*)(void)),
    "max_align_t covers function pointers");
_Static_assert(_Alignof(max_align_t) == __BIGGEST_ALIGNMENT__,
    "max_align_t is the compiler's greatest fundamental alignment");
_Static_assert(sizeof(size_t) == sizeof(sizeof(int)), "size_t matches sizeof");
_Static_assert(sizeof(ptrdiff_t) == sizeof((char *)0 - (char *)0),
    "ptrdiff_t matches pointer difference");

struct offset_probe {
	char		first;
	max_align_t	aligned;
};
_Static_assert(offsetof(struct offset_probe, aligned) ==
    _Alignof(max_align_t), "offsetof sees max_align_t padding");
