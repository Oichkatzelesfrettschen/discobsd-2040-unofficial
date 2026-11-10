#include <limits.h>
#include <stdlib.h>

/*
 * C17 7.22p2-p3 and 7.22.7-7.22.8: <stdlib.h> declares wchar_t, MB_CUR_MAX
 * as a size_t no greater than MB_LEN_MAX, and the five multibyte functions
 * with these types.
 */
_Static_assert(_Generic(MB_CUR_MAX, size_t: 1, default: 0),
    "MB_CUR_MAX has type size_t");
_Static_assert(MB_CUR_MAX >= 1 && MB_CUR_MAX <= MB_LEN_MAX,
    "MB_CUR_MAX lies between 1 and MB_LEN_MAX");
_Static_assert(_Generic(L'a', wchar_t: 1, default: 0),
    "the <stdlib.h> wchar_t is the compiler's");

int (*const mblen_probe)(const char *, size_t) = mblen;
int (*const mbtowc_probe)(wchar_t *, const char *, size_t) = mbtowc;
int (*const wctomb_probe)(char *, wchar_t) = wctomb;
size_t (*const mbstowcs_probe)(wchar_t *, const char *, size_t) = mbstowcs;
size_t (*const wcstombs_probe)(char *, const wchar_t *, size_t) = wcstombs;
