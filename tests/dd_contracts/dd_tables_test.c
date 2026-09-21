/*
 * The conversion tables in bin/dd/dd.c, checked against the three the
 * program used to carry.
 *
 * atoe and etoa are bijections and exact inverses over all 256 bytes.
 * atoibm repeated atoe at 252 of its 256 entries, so dd now spells it as
 * those four exceptions over atoe; this suite holds that decomposition to
 * the whole of the table it replaced, entry by entry. atoibm is not a
 * bijection: 91 and 213 both reach 173, and 93 and 229 both reach 189, so
 * it has no inverse, which is why conv=ascii reads etoa and why that
 * collision is asserted here rather than left to be turned into one.
 */
#include <stdio.h>

extern const char atoe[];
extern const char etoa[];
int atoibm(int);

static unsigned checks, failures;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,	\
		    #cond);						\
	}								\
} while (0)

/* The table dd carried before the decomposition, verbatim. */
static const unsigned char atoibm_reference[256] = {
	0000, 0001, 0002, 0003, 0067, 0055, 0056, 0057,
	0026, 0005, 0045, 0013, 0014, 0015, 0016, 0017,
	0020, 0021, 0022, 0023, 0074, 0075, 0062, 0046,
	0030, 0031, 0077, 0047, 0034, 0035, 0036, 0037,
	0100, 0132, 0177, 0173, 0133, 0154, 0120, 0175,
	0115, 0135, 0134, 0116, 0153, 0140, 0113, 0141,
	0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367,
	0370, 0371, 0172, 0136, 0114, 0176, 0156, 0157,
	0174, 0301, 0302, 0303, 0304, 0305, 0306, 0307,
	0310, 0311, 0321, 0322, 0323, 0324, 0325, 0326,
	0327, 0330, 0331, 0342, 0343, 0344, 0345, 0346,
	0347, 0350, 0351, 0255, 0340, 0275, 0137, 0155,
	0171, 0201, 0202, 0203, 0204, 0205, 0206, 0207,
	0210, 0211, 0221, 0222, 0223, 0224, 0225, 0226,
	0227, 0230, 0231, 0242, 0243, 0244, 0245, 0246,
	0247, 0250, 0251, 0300, 0117, 0320, 0241, 0007,
	0040, 0041, 0042, 0043, 0044, 0025, 0006, 0027,
	0050, 0051, 0052, 0053, 0054, 0011, 0012, 0033,
	0060, 0061, 0032, 0063, 0064, 0065, 0066, 0010,
	0070, 0071, 0072, 0073, 0004, 0024, 0076, 0341,
	0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110,
	0111, 0121, 0122, 0123, 0124, 0125, 0126, 0127,
	0130, 0131, 0142, 0143, 0144, 0145, 0146, 0147,
	0150, 0151, 0160, 0161, 0162, 0163, 0164, 0165,
	0166, 0167, 0170, 0200, 0212, 0213, 0214, 0215,
	0216, 0217, 0220, 0232, 0233, 0234, 0235, 0236,
	0237, 0240, 0252, 0253, 0254, 0255, 0256, 0257,
	0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
	0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277,
	0312, 0313, 0314, 0315, 0316, 0317, 0332, 0333,
	0334, 0335, 0336, 0337, 0352, 0353, 0354, 0355,
	0356, 0357, 0372, 0373, 0374, 0375, 0376, 0377,
};

int
main(void)
{
	int i, j;
	int seen[256];

	/* atoe and etoa are inverses in both directions, over every byte. */
	for (i = 0; i < 256; i++) {
		CHECK((etoa[atoe[i] & 0377] & 0377) == i);
		CHECK((atoe[etoa[i] & 0377] & 0377) == i);
	}

	/*
	 * Each is a bijection, which is what makes the above an inverse
	 * rather than a coincidence on the bytes that happen to round-trip.
	 */
	for (i = 0; i < 256; i++)
		seen[i] = 0;
	for (i = 0; i < 256; i++)
		seen[atoe[i] & 0377]++;
	for (i = 0; i < 256; i++)
		CHECK(seen[i] == 1);
	for (i = 0; i < 256; i++)
		seen[i] = 0;
	for (i = 0; i < 256; i++)
		seen[etoa[i] & 0377]++;
	for (i = 0; i < 256; i++)
		CHECK(seen[i] == 1);

	/*
	 * The decomposition reproduces the table it replaced, entry by
	 * entry. This is the whole of the refactor's correctness.
	 */
	for (i = 0; i < 256; i++)
		CHECK(atoibm(i) == atoibm_reference[i]);

	/* It differs from atoe at exactly four inputs, and nowhere else. */
	j = 0;
	for (i = 0; i < 256; i++)
		if (atoibm(i) != (atoe[i] & 0377))
			j++;
	CHECK(j == 4);
	CHECK(atoibm(33) == 90 && (atoe[33] & 0377) == 79);
	CHECK(atoibm(91) == 173 && (atoe[91] & 0377) == 74);
	CHECK(atoibm(93) == 189 && (atoe[93] & 0377) == 90);
	CHECK(atoibm(124) == 79 && (atoe[124] & 0377) == 106);

	/*
	 * It is not a bijection, so conv=ibm does not round-trip and no
	 * inverse table of it can exist.
	 */
	for (i = 0; i < 256; i++)
		seen[i] = 0;
	for (i = 0; i < 256; i++)
		seen[atoibm(i)]++;
	j = 0;
	for (i = 0; i < 256; i++)
		if (seen[i] != 0)
			j++;
	CHECK(j == 254);
	CHECK(atoibm(91) == atoibm(213));
	CHECK(atoibm(93) == atoibm(229));

	/*
	 * The space a short conversion block is padded with is the one place
	 * the tables are read outside the per-byte path.
	 */
	CHECK((atoe[0x20] & 0377) == 64);
	CHECK(atoibm(0x20) == 64);

	fprintf(stderr, "dd_tables: %u checks, %u failures\n", checks,
	    failures);
	return (failures == 0 ? 0 : 1);
}
