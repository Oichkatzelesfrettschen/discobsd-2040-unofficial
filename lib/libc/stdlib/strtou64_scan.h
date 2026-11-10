/*
 * The subject-sequence scan of the 64-bit conversions of C17 7.22.1.4.  The
 * including file defines STRTOU64_SCAN, the function name; STRTOU64_CHAR,
 * the element type of the string and end pointer; and STRTOU64_UNIT, the
 * type each element is read as.  strtoull.c instantiates it over char read
 * as unsigned char.  Each instantiation depends on <errno.h>, <limits.h>,
 * <stdint.h> and "strtou64.h".
 */

/*
 * The magnitude accumulates in four 16-bit limbs, least significant first.
 * One step multiplies every limb by the base and adds a carry, and each
 * product stays below 2^32 for every base C17 7.22.1.4 accepts, so the scan
 * uses the 32-bit multiply ARMv6-M has and calls no 64-bit multiply or
 * divide routine; libc links none.  A carry out of the top limb is the
 * overflow test, which replaces the divide-derived cutoff strtoul uses.
 */
#define	LIMBS		4
#define	LIMB_BITS	16
#define	LIMB_MASK	0xffffU

struct strtou64_result
STRTOU64_SCAN(const STRTOU64_CHAR *string, STRTOU64_CHAR **end_pointer,
    int base)
{
	struct strtou64_result result;
	const STRTOU64_UNIT *cursor;
	uint32_t limbs[LIMBS];
	uint32_t carry;
	STRTOU64_UNIT character;
	int digit, converted, limb;

	result.magnitude = 0;
	result.negative = 0;
	result.overflow = 0;
	for (limb = 0; limb < LIMBS; limb++)
		limbs[limb] = 0;
	if (end_pointer != NULL)
		*end_pointer = (STRTOU64_CHAR *)string;
	if (base != 0 && (base < 2 || base > 36)) {
		errno = EINVAL;
		return result;
	}

	/*
	 * The library has no setlocale(), so the six C-locale white-space
	 * characters are the whole class and the scan stays independent of the
	 * ctype table.
	 */
	cursor = (const STRTOU64_UNIT *)string;
	do {
		character = *cursor++;
	} while (character == ' ' || character == '\t' ||
	    character == '\n' || character == '\v' ||
	    character == '\f' || character == '\r');
	if (character == '-') {
		result.negative = 1;
		character = *cursor++;
	} else if (character == '+')
		character = *cursor++;

	/*
	 * A 0x prefix belongs to the subject sequence only when a hex digit
	 * follows it; otherwise the sequence is the zero alone.
	 */
	if ((base == 0 || base == 16) && character == '0' &&
	    (*cursor == 'x' || *cursor == 'X') &&
	    ((cursor[1] >= '0' && cursor[1] <= '9') ||
	    (cursor[1] >= 'a' && cursor[1] <= 'f') ||
	    (cursor[1] >= 'A' && cursor[1] <= 'F'))) {
		character = cursor[1];
		cursor += 2;
		base = 16;
	} else if (base == 0)
		base = character == '0' ? 8 : 10;

	for (converted = 0;; character = *cursor++) {
		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (character >= 'a' && character <= 'z')
			digit = character - 'a' + 10;
		else if (character >= 'A' && character <= 'Z')
			digit = character - 'A' + 10;
		else
			break;
		if (digit >= base)
			break;
		converted = 1;
		/* The end pointer follows every digit of an overflowed value. */
		if (result.overflow)
			continue;
		carry = (uint32_t)digit;
		for (limb = 0; limb < LIMBS; limb++) {
			carry += limbs[limb] * (uint32_t)base;
			limbs[limb] = carry & LIMB_MASK;
			carry >>= LIMB_BITS;
		}
		if (carry != 0)
			result.overflow = 1;
	}
	if (converted && end_pointer != NULL)
		*end_pointer = (STRTOU64_CHAR *)(cursor - 1);
	if (result.overflow)
		result.magnitude = ULLONG_MAX;
	else
		result.magnitude =
		    ((unsigned long long)(limbs[3] << LIMB_BITS | limbs[2]) << 32) |
		    (limbs[1] << LIMB_BITS | limbs[0]);
	return result;
}
