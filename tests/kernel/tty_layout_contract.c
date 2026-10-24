#define KERNEL 1
#include <sys/tty.h>

#ifndef TTY_LAYOUT_EXPECTED_SIZE
#define TTY_LAYOUT_EXPECTED_SIZE 120
#endif

_Static_assert(sizeof(struct tty) == TTY_LAYOUT_EXPECTED_SIZE,
    "struct tty size changed");
_Static_assert(_Alignof(struct tty) == 4, "struct tty alignment changed");
_Static_assert(__builtin_offsetof(struct tty, t_flags) == 60,
    "struct tty t_flags offset changed");
_Static_assert(sizeof(((struct tty *)0)->t_flags) == 4,
    "struct tty t_flags width changed");
_Static_assert(__builtin_offsetof(struct tty, t_state) == 64,
    "struct tty t_state offset changed");
_Static_assert(__builtin_offsetof(struct tty, t_pgrp) == 68,
    "struct tty t_pgrp offset changed");

void
tty_layout_contract(void)
{
}
