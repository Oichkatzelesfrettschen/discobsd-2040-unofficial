#include "rom_float.inc"

ROM_FLOAT_SETUP
ROM_FLOAT_DOUBLE_TARGET(dmul, __aeabi_dmul, 0x4453, 0x08, 2)
ROM_FLOAT_TEXT
ROM_FLOAT_ALIAS(__muldf3, __aeabi_dmul)
