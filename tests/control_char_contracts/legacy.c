#define CRIBBAGE_CONTROL(parameter) ('X' - 'A' + 1)

_Static_assert(CRIBBAGE_CONTROL(G) == 7, "control-G");
_Static_assert(CRIBBAGE_CONTROL(L) == 12, "control-L");

#define TIP_CONTROL(parameter) ('c' & 037)

_Static_assert(TIP_CONTROL(a) == 1, "control-A");
_Static_assert(TIP_CONTROL(d) == 4, "control-D");
_Static_assert(TIP_CONTROL(p) == 16, "control-P");
_Static_assert(TIP_CONTROL(y) == 25, "control-Y");
_Static_assert(TIP_CONTROL(z) == 26, "control-Z");
