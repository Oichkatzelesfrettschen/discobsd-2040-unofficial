#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_DOUBLE_TARGET(ddiv, __aeabi_ddiv, 0x4453, 0x0c, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__divdf3, __aeabi_ddiv)
