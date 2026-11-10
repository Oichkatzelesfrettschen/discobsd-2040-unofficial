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

/*
 * C17 6.4.4.4p11 and 6.4.5p6: a wide character constant has type wchar_t and
 * a wide string literal is an array of it, so the typedef must be the
 * compiler's type and not merely one of the same width.
 */
_Static_assert(_Generic(L'a', wchar_t: 1, default: 0),
    "a wide character constant has type wchar_t");
_Static_assert(_Generic(&L"a"[0], wchar_t *: 1, default: 0),
    "a wide string literal is an array of wchar_t");
