#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(ul2d, __aeabi_ul2d, 0x4453, 0x64, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__floatundidf, __aeabi_ul2d)
