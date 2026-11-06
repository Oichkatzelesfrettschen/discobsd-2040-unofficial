/*
 * Base64 per RFC 4648 section 4: the standard alphabet, '=' padding, no
 * line breaks.
 */

#include "calc_internal.h"

static const char alphabet[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

calc_status
calc_b64_encode(const uint8_t *in, size_t n, char *out, size_t cap, size_t *written)
{
	size_t groups = n / 3 + (n % 3 != 0), need, i, o = 0;
	uint32_t v;

	if ((in == NULL && n != 0) || (out == NULL && cap != 0) || written == NULL)
		return CALC_E_ARG;
	if (groups > ((size_t)-1) / 4)
		return CALC_E_SIZE;
	need = groups * 4;
	if (cap < need)
		return CALC_E_SIZE;
	for (i = 0; i + 3 <= n; i += 3) {
		v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
		out[o++] = alphabet[(v >> 18) & 63];
		out[o++] = alphabet[(v >> 12) & 63];
		out[o++] = alphabet[(v >> 6) & 63];
		out[o++] = alphabet[v & 63];
	}
	if (n - i == 1) {
		v = (uint32_t)in[i] << 16;
		out[o++] = alphabet[(v >> 18) & 63];
		out[o++] = alphabet[(v >> 12) & 63];
		out[o++] = '=';
		out[o++] = '=';
	} else if (n - i == 2) {
		v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
		out[o++] = alphabet[(v >> 18) & 63];
		out[o++] = alphabet[(v >> 12) & 63];
		out[o++] = alphabet[(v >> 6) & 63];
		out[o++] = '=';
	}
	*written = o;
	return CALC_OK;
}

/* Value of an alphabet character, 64 for '=' and -1 for anything else. */
static int
b64_value(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	if (c == '=')
		return 64;
	return -1;
}

static int
b64_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/*
 * Walk the input once. With out == NULL the walk only validates and counts;
 * with a buffer it also stores the bytes. Padding may only end the final
 * group: "x===" and padding followed by data are malformed.
 */
static calc_status
b64_walk(const char *in, size_t n, uint8_t *out, size_t *count)
{
	uint32_t acc = 0;
	unsigned have = 0, pad = 0;
	size_t i, o = 0;
	int v;

	for (i = 0; i < n; i++) {
		if (b64_space(in[i]))
			continue;
		v = b64_value(in[i]);
		if (v < 0)
			return CALC_E_INVALID;
		if (v == 64) {
			if (have < 2)
				return CALC_E_INVALID;
			pad++;
			acc <<= 6;
			have++;
		} else {
			if (pad != 0)
				return CALC_E_INVALID;
			acc = (acc << 6) | (uint32_t)v;
			have++;
		}
		if (have == 4) {
			if (pad > 2)
				return CALC_E_INVALID;
			if (out != NULL) {
				out[o] = (uint8_t)(acc >> 16);
				if (pad < 2)
					out[o + 1] = (uint8_t)(acc >> 8);
				if (pad < 1)
					out[o + 2] = (uint8_t)acc;
			}
			o += 3 - pad;
			acc = 0;
			have = 0;
			if (pad != 0) {
				/* Only whitespace may follow the padded group. */
				for (i++; i < n; i++)
					if (!b64_space(in[i]))
						return CALC_E_INVALID;
				break;
			}
		}
	}
	if (have != 0)
		return CALC_E_INVALID;
	*count = o;
	return CALC_OK;
}

calc_status
calc_b64_decode(const char *in, size_t n, uint8_t *out, size_t cap, size_t *written)
{
	size_t count;
	calc_status st;

	if ((in == NULL && n != 0) || written == NULL)
		return CALC_E_ARG;
	st = b64_walk(in, n, NULL, &count);
	if (st != CALC_OK)
		return st;
	if (count > cap)
		return CALC_E_SIZE;
	if (count != 0 && out == NULL)
		return CALC_E_ARG;
	st = b64_walk(in, n, out, &count);
	if (st != CALC_OK)
		return st;
	*written = count;
	return CALC_OK;
}
