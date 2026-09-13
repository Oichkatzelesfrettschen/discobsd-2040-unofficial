#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(l2d, __aeabi_l2d, 0x4453, 0x5c, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__floatdidf, __aeabi_l2d)
