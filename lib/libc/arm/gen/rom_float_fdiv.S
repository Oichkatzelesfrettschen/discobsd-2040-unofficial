#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(fdiv, __aeabi_fdiv, 0x4653, 0x0c, 1)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__divsf3, __aeabi_fdiv)
