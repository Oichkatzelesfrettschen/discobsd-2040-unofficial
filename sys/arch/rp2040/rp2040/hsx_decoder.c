/*
 * The packed executable decoder: the vendored heatshrink decoder compiled
 * a second time with the parameters and entry point names hsx_decoder.h
 * sets. The source stays byte-identical to upstream.
 */
#include <machine/hsx_decoder.h>
#include "../heatshrink/heatshrink_decoder.c"
