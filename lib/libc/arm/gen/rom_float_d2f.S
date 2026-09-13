#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(d2f, __aeabi_d2f, 0x4453, 0x7c, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__truncdfsf2, __aeabi_d2f)
