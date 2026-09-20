#define CONTROL(character) ((character) & 037)

_Static_assert(CONTROL('a') == 1, "control-A");
_Static_assert(CONTROL('d') == 4, "control-D");
_Static_assert(CONTROL('g') == 7, "control-G");
_Static_assert(CONTROL('l') == 12, "control-L");
_Static_assert(CONTROL('p') == 16, "control-P");
_Static_assert(CONTROL('y') == 25, "control-Y");
_Static_assert(CONTROL('z') == 26, "control-Z");
