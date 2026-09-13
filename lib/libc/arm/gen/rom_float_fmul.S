#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_SINGLE_TARGET(fmul, __aeabi_fmul, 0x4653, 0x08, 1)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__mulsf3, __aeabi_fmul)
