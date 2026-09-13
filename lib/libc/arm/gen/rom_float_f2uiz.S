#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(f2uiz, __aeabi_f2uiz, 0x4653, 0x24, 1)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__fixunssfsi, __aeabi_f2uiz)
