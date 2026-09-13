#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(f2d, __aeabi_f2d, 0x4653, 0x7c, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__extendsfdf2, __aeabi_f2d)
