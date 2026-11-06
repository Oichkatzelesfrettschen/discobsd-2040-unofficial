/*
 * The parts of Simon Tatham's portable puzzle collection that the puzzles of
 * games/puzzlebox call: random numbers from a seed (random.c), allocation
 * with the collection's checks (malloc.c), the shuffle, cursor and string
 * helpers (misc.c) and the combination iterator (combi.c). The sources sit
 * unchanged in tatham/. They are compiled here as one unit with a section
 * per function, and the link discards the helpers the text front end never
 * reaches (colour mixing and drawing helpers that need the collection's
 * graphics layer).
 *
 * The collection's sources carry no warning policy of this tree: the
 * diagnostics below are accepted for the imported text only.
 */
/* The tree has no <tgmath.h>; the sources include <math.h> instead. */
#define NO_TGMATH_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "tatham/misc.c"
#include "tatham/random.c"
#include "tatham/malloc.c"
#include "tatham/combi.c"
#pragma GCC diagnostic pop
