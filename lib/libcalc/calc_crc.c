/*
 * Generic CRC over the Rocksoft parameterized model for widths 1 to 64, plus
 * the catalog parameter sets the calculator offers as presets.
 *
 * The register advances one message bit at a time: the bit shifted out of
 * the top is XORed with the incoming message bit and, when set, the
 * polynomial is XORed into the shifted register. That recurrence is the
 * definition of the model for every width, so no width has a code path of
 * its own and the register never carries bits above the width.
 */

#include "calc_internal.h"

const calc_crc_spec calc_crc8 = { 8, 0x07, 0x00, 0x00, 0, 0 };
const calc_crc_spec calc_crc8_maxim = { 8, 0x31, 0x00, 0x00, 1, 1 };
const calc_crc_spec calc_crc12_dect = { 12, 0x80F, 0x000, 0x000, 0, 0 };
const calc_crc_spec calc_crc12_umts = { 12, 0x80F, 0x000, 0x000, 0, 1 };
const calc_crc_spec calc_crc16_arc = { 16, 0x8005, 0x0000, 0x0000, 1, 1 };
const calc_crc_spec calc_crc16_ccitt_false = { 16, 0x1021, 0xFFFF, 0x0000, 0, 0 };
const calc_crc_spec calc_crc16_xmodem = { 16, 0x1021, 0x0000, 0x0000, 0, 0 };
const calc_crc_spec calc_crc32 = { 32, 0x04C11DB7UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 1, 1 };
const calc_crc_spec calc_crc32c = { 32, 0x1EDC6F41UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 1, 1 };
const calc_crc_spec calc_crc32_bzip2 = { 32, 0x04C11DB7UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0, 0 };
const calc_crc_spec calc_crc64_ecma = { 64, 0x42F0E1EBA9EA3693ULL, 0, 0, 0, 0 };

/* Reverse the low `bits` bits of v. */
static uint64_t
reflect(uint64_t v, unsigned bits)
{
	uint64_t r = 0;
	unsigned i;

	for (i = 0; i < bits; i++) {
		r = (r << 1) | (v & 1);
		v >>= 1;
	}
	return r;
}

calc_status
calc_crc(const calc_crc_spec *spec, const uint8_t *data, size_t len, uint64_t *out)
{
	uint64_t mask, top, reg, poly;
	unsigned w, bit;
	unsigned b;
	size_t i;
	uint64_t fb;

	if (spec == NULL || out == NULL || (data == NULL && len != 0))
		return CALC_E_ARG;
	w = spec->width;
	if (w < 1 || w > 64)
		return CALC_E_ARG;
	mask = w == 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1);
	if ((spec->poly & ~mask) != 0 || (spec->init & ~mask) != 0 ||
	    (spec->xorout & ~mask) != 0 || spec->refin > 1 || spec->refout > 1)
		return CALC_E_ARG;
	top = (uint64_t)1 << (w - 1);
	poly = spec->poly;
	reg = spec->init;
	for (i = 0; i < len; i++) {
		b = data[i];
		if (spec->refin)
			b = (unsigned)reflect(b, 8);
		for (bit = 0x80; bit != 0; bit >>= 1) {
			fb = ((reg & top) != 0) ^ ((b & bit) != 0);
			reg = (reg << 1) & mask;
			if (fb)
				reg ^= poly;
		}
	}
	if (spec->refout)
		reg = reflect(reg, w);
	*out = reg ^ spec->xorout;
	return CALC_OK;
}
