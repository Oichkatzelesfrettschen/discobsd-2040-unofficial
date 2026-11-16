#define KERNEL 1
#include <sys/tty.h>

#ifndef TTY_LAYOUT_EXPECTED_SIZE
#define TTY_LAYOUT_EXPECTED_SIZE 100
#endif

_Static_assert(sizeof(struct tty) == TTY_LAYOUT_EXPECTED_SIZE,
    "struct tty size changed");
_Static_assert(_Alignof(struct tty) == 4, "struct tty alignment changed");
_Static_assert(__builtin_offsetof(struct tty, t_flags) == 56,
    "struct tty t_flags offset changed");
_Static_assert(sizeof(((struct tty *)0)->t_flags) == 4,
    "struct tty t_flags width changed");
_Static_assert(__builtin_offsetof(struct tty, t_state) == 60,
    "struct tty t_state offset changed");
_Static_assert(__builtin_offsetof(struct tty, t_pgrp) == 64,
    "struct tty t_pgrp offset changed");
_Static_assert(sizeof(((struct tty *)0)->t_pgrp) == 2,
    "struct tty t_pgrp width changed");
_Static_assert(__builtin_offsetof(struct tty, t_ospeed) == 75,
    "struct tty t_ospeed offset changed");
_Static_assert(__builtin_offsetof(struct tty, t_chars) == 76,
    "struct tty t_chars offset changed: padding precedes the character set");
_Static_assert(sizeof(((struct tty *)0)->t_ospeed) == 1,
    "struct tty t_ospeed width changed");

void
tty_layout_contract(void)
{
}
