#include <inttypes.h>

/*
 * Local prototypes carry the format attribute, so -Wformat under -Werror
 * compares every macro below with the type of its argument.  A macro that
 * names the wrong length modifier fails the translation.
 */
int print_probe(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int scan_probe(const char *, ...) __attribute__((__format__(__scanf__, 1, 2)));
void inttypes_contract(void);

void
inttypes_contract(void)
{
	int8_t signed_8 = 0;
	uint8_t unsigned_8 = 0;
	int16_t signed_16 = 0;
	uint16_t unsigned_16 = 0;
	int32_t signed_32 = 0;
	uint32_t unsigned_32 = 0;
	int64_t signed_64 = 0;
	uint64_t unsigned_64 = 0;
	int_least8_t signed_least8 = 0;
	uint_least8_t unsigned_least8 = 0;
	int_least16_t signed_least16 = 0;
	uint_least16_t unsigned_least16 = 0;
	int_least32_t signed_least32 = 0;
	uint_least32_t unsigned_least32 = 0;
	int_least64_t signed_least64 = 0;
	uint_least64_t unsigned_least64 = 0;
	int_fast8_t signed_fast8 = 0;
	uint_fast8_t unsigned_fast8 = 0;
	int_fast16_t signed_fast16 = 0;
	uint_fast16_t unsigned_fast16 = 0;
	int_fast32_t signed_fast32 = 0;
	uint_fast32_t unsigned_fast32 = 0;
	int_fast64_t signed_fast64 = 0;
	uint_fast64_t unsigned_fast64 = 0;
	intmax_t signed_max = 0;
	uintmax_t unsigned_max = 0;
	intptr_t signed_ptr = 0;
	uintptr_t unsigned_ptr = 0;

	print_probe("%" PRId8 "%" PRIi8, signed_8, signed_8);
	print_probe("%" PRIo8 "%" PRIu8 "%" PRIx8 "%" PRIX8,
	    unsigned_8, unsigned_8, unsigned_8, unsigned_8);
	print_probe("%" PRId16 "%" PRIi16, signed_16, signed_16);
	print_probe("%" PRIo16 "%" PRIu16 "%" PRIx16 "%" PRIX16,
	    unsigned_16, unsigned_16, unsigned_16, unsigned_16);
	print_probe("%" PRId32 "%" PRIi32, signed_32, signed_32);
	print_probe("%" PRIo32 "%" PRIu32 "%" PRIx32 "%" PRIX32,
	    unsigned_32, unsigned_32, unsigned_32, unsigned_32);
	print_probe("%" PRId64 "%" PRIi64, signed_64, signed_64);
	print_probe("%" PRIo64 "%" PRIu64 "%" PRIx64 "%" PRIX64,
	    unsigned_64, unsigned_64, unsigned_64, unsigned_64);
	print_probe("%" PRIdLEAST8 "%" PRIiLEAST8, signed_least8, signed_least8);
	print_probe("%" PRIoLEAST8 "%" PRIuLEAST8 "%" PRIxLEAST8 "%" PRIXLEAST8,
	    unsigned_least8, unsigned_least8, unsigned_least8, unsigned_least8);
	print_probe("%" PRIdLEAST16 "%" PRIiLEAST16, signed_least16, signed_least16);
	print_probe("%" PRIoLEAST16 "%" PRIuLEAST16 "%" PRIxLEAST16 "%" PRIXLEAST16,
	    unsigned_least16, unsigned_least16, unsigned_least16, unsigned_least16);
	print_probe("%" PRIdLEAST32 "%" PRIiLEAST32, signed_least32, signed_least32);
	print_probe("%" PRIoLEAST32 "%" PRIuLEAST32 "%" PRIxLEAST32 "%" PRIXLEAST32,
	    unsigned_least32, unsigned_least32, unsigned_least32, unsigned_least32);
	print_probe("%" PRIdLEAST64 "%" PRIiLEAST64, signed_least64, signed_least64);
	print_probe("%" PRIoLEAST64 "%" PRIuLEAST64 "%" PRIxLEAST64 "%" PRIXLEAST64,
	    unsigned_least64, unsigned_least64, unsigned_least64, unsigned_least64);
	print_probe("%" PRIdFAST8 "%" PRIiFAST8, signed_fast8, signed_fast8);
	print_probe("%" PRIoFAST8 "%" PRIuFAST8 "%" PRIxFAST8 "%" PRIXFAST8,
	    unsigned_fast8, unsigned_fast8, unsigned_fast8, unsigned_fast8);
	print_probe("%" PRIdFAST16 "%" PRIiFAST16, signed_fast16, signed_fast16);
	print_probe("%" PRIoFAST16 "%" PRIuFAST16 "%" PRIxFAST16 "%" PRIXFAST16,
	    unsigned_fast16, unsigned_fast16, unsigned_fast16, unsigned_fast16);
	print_probe("%" PRIdFAST32 "%" PRIiFAST32, signed_fast32, signed_fast32);
	print_probe("%" PRIoFAST32 "%" PRIuFAST32 "%" PRIxFAST32 "%" PRIXFAST32,
	    unsigned_fast32, unsigned_fast32, unsigned_fast32, unsigned_fast32);
	print_probe("%" PRIdFAST64 "%" PRIiFAST64, signed_fast64, signed_fast64);
	print_probe("%" PRIoFAST64 "%" PRIuFAST64 "%" PRIxFAST64 "%" PRIXFAST64,
	    unsigned_fast64, unsigned_fast64, unsigned_fast64, unsigned_fast64);
	print_probe("%" PRIdMAX "%" PRIiMAX, signed_max, signed_max);
	print_probe("%" PRIoMAX "%" PRIuMAX "%" PRIxMAX "%" PRIXMAX,
	    unsigned_max, unsigned_max, unsigned_max, unsigned_max);
	print_probe("%" PRIdPTR "%" PRIiPTR, signed_ptr, signed_ptr);
	print_probe("%" PRIoPTR "%" PRIuPTR "%" PRIxPTR "%" PRIXPTR,
	    unsigned_ptr, unsigned_ptr, unsigned_ptr, unsigned_ptr);

	scan_probe("%" SCNd8 "%" SCNi8, &signed_8, &signed_8);
	scan_probe("%" SCNo8 "%" SCNu8 "%" SCNx8,
	    &unsigned_8, &unsigned_8, &unsigned_8);
	scan_probe("%" SCNd16 "%" SCNi16, &signed_16, &signed_16);
	scan_probe("%" SCNo16 "%" SCNu16 "%" SCNx16,
	    &unsigned_16, &unsigned_16, &unsigned_16);
	scan_probe("%" SCNd32 "%" SCNi32, &signed_32, &signed_32);
	scan_probe("%" SCNo32 "%" SCNu32 "%" SCNx32,
	    &unsigned_32, &unsigned_32, &unsigned_32);
	scan_probe("%" SCNdLEAST8 "%" SCNiLEAST8, &signed_least8, &signed_least8);
	scan_probe("%" SCNoLEAST8 "%" SCNuLEAST8 "%" SCNxLEAST8,
	    &unsigned_least8, &unsigned_least8, &unsigned_least8);
	scan_probe("%" SCNdLEAST16 "%" SCNiLEAST16, &signed_least16, &signed_least16);
	scan_probe("%" SCNoLEAST16 "%" SCNuLEAST16 "%" SCNxLEAST16,
	    &unsigned_least16, &unsigned_least16, &unsigned_least16);
	scan_probe("%" SCNdLEAST32 "%" SCNiLEAST32, &signed_least32, &signed_least32);
	scan_probe("%" SCNoLEAST32 "%" SCNuLEAST32 "%" SCNxLEAST32,
	    &unsigned_least32, &unsigned_least32, &unsigned_least32);
	scan_probe("%" SCNdFAST8 "%" SCNiFAST8, &signed_fast8, &signed_fast8);
	scan_probe("%" SCNoFAST8 "%" SCNuFAST8 "%" SCNxFAST8,
	    &unsigned_fast8, &unsigned_fast8, &unsigned_fast8);
	scan_probe("%" SCNdFAST16 "%" SCNiFAST16, &signed_fast16, &signed_fast16);
	scan_probe("%" SCNoFAST16 "%" SCNuFAST16 "%" SCNxFAST16,
	    &unsigned_fast16, &unsigned_fast16, &unsigned_fast16);
	scan_probe("%" SCNdFAST32 "%" SCNiFAST32, &signed_fast32, &signed_fast32);
	scan_probe("%" SCNoFAST32 "%" SCNuFAST32 "%" SCNxFAST32,
	    &unsigned_fast32, &unsigned_fast32, &unsigned_fast32);
	scan_probe("%" SCNdPTR "%" SCNiPTR, &signed_ptr, &signed_ptr);
	scan_probe("%" SCNoPTR "%" SCNuPTR "%" SCNxPTR,
	    &unsigned_ptr, &unsigned_ptr, &unsigned_ptr);
}

/*
 * _doscan has no 64-bit store, so these macros stay undefined.  The check
 * turns a definition added ahead of that store into a translation failure.
 */
#if defined(SCNd64) || \
    defined(SCNi64) || \
    defined(SCNo64) || \
    defined(SCNu64) || \
    defined(SCNx64) || \
    defined(SCNdLEAST64) || \
    defined(SCNiLEAST64) || \
    defined(SCNoLEAST64) || \
    defined(SCNuLEAST64) || \
    defined(SCNxLEAST64) || \
    defined(SCNdFAST64) || \
    defined(SCNiFAST64) || \
    defined(SCNoFAST64) || \
    defined(SCNuFAST64) || \
    defined(SCNxFAST64) || \
    defined(SCNdMAX) || \
    defined(SCNiMAX) || \
    defined(SCNoMAX) || \
    defined(SCNuMAX) || \
    defined(SCNxMAX)
#error "a 64-bit fscanf macro requires a 64-bit store in _doscan"
#endif

_Static_assert(sizeof(((imaxdiv_t *)0)->quot) == sizeof(intmax_t) &&
    sizeof(((imaxdiv_t *)0)->rem) == sizeof(intmax_t),
    "imaxdiv_t members are intmax_t");
_Static_assert(sizeof(PRId64) == sizeof("lld") && sizeof(PRIdMAX) == sizeof("jd"),
    "the macros are string literals");
