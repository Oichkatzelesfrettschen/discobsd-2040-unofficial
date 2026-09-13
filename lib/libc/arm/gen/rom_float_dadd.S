#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_DOUBLE_TARGET(dadd, __aeabi_dadd, 0x4453, 0x00, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__adddf3, __aeabi_dadd)
