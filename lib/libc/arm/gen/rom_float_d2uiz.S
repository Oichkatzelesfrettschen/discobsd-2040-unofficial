#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(d2uiz, __aeabi_d2uiz, 0x4453, 0x24, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__fixunsdfsi, __aeabi_d2uiz)
