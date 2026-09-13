#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(fadd, __aeabi_fadd, 0x4653, 0x00, 1)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__addsf3, __aeabi_fadd)
