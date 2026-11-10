#include <stdalign.h>
#include <stddef.h>

#if __alignas_is_defined != 1 || __alignof_is_defined != 1
#error "C17 7.15p3 feature macros expand to the integer constant 1"
#endif

_Static_assert(alignof(max_align_t) == _Alignof(max_align_t),
    "alignof names _Alignof");

struct aligned_probe {
	char		first;
	alignas(8) char	second;
};
_Static_assert(offsetof(struct aligned_probe, second) == 8,
    "alignas raises a member's alignment");
_Static_assert(alignof(struct aligned_probe) == 8,
    "alignas reaches the containing structure");

struct type_form_probe {
	char			first;
	alignas(max_align_t) char second;
};
_Static_assert(offsetof(struct type_form_probe, second) ==
    alignof(max_align_t), "alignas accepts a type name");
