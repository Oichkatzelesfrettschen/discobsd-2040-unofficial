/* Original MATEzinho code is under the repository's BSD 3-Clause license. */
#ifndef MZFONT_H
#define MZFONT_H

/*
 * A glyph table for printable ASCII (32..126): glyph c is height rows of
 * width[c - 32] pixels at bits[offset[c - 32]], each row packed MSB first
 * and padded to a byte, bpp 1 (ink or not) or 4 (coverage 0..15). ascent
 * is the baseline's distance below the top row.
 */
struct mz_font {
    unsigned char height, ascent, bpp;
    const unsigned char *width;
    const unsigned short *offset;
    const unsigned char *bits;
};
extern const struct mz_font MZ_FONT_UI, MZ_FONT_MONO;

#endif
