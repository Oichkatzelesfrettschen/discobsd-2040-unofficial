# C17 libc handoff

State of the C17 library work on branch `claude/susv4-v5-c17-libc-rywhg7`,
from base `b3f293a6`. `c17-library-closure.md` holds the full record:
interfaces, invariants, measured accuracy and the NetBSD comparison.

## Delivered

- Every C17 clause 7 interface except those listed under Open: `<iso646.h>`,
  `div`/`ldiv`/`lldiv`/`llabs`, `<locale.h>`, `strcoll`, `strxfrm`, `clock`,
  `timespec_get`, the C11 `<float.h>` macros, all C99/C17 `<math.h>`
  functions in three precisions, `<fenv.h>`, `<tgmath.h>`, `<wchar.h>`,
  `<wctype.h>`, `<uchar.h>`.
- `lib/libc/stdlib/strtofp_core.h`: correctly rounded decimal, hexadecimal,
  inf and nan conversion behind `strtod`, `strtof`, `wcstof` and `wcstod`.
- Gates: `check-libc-integer-division`, `check-libc-c17-locale`, `-strtof`,
  `-time`, `-math` and `-wide`, the `c17_library_contract.c` provider check
  in `check-c17-headers-cross`, and `check-libc-c17-netbsd` (needs
  `NETBSD_SRC`, outside every aggregate).

## Open

1. **strtod.** Approved by the owner and done: `strtod` now rounds through
   `strtofp_core.h` and accepts inf, nan and hexadecimal input. The core is
   a header of static functions so the member still builds and links alone;
   `strtod.o` grows from 692 to 2,698 bytes of text, with a deepest frame of
   about 1.45 KB, in `trek`, `primes` and the native archives.
2. **Native archives.** The new members are in the cross ELF archives only;
   `lib/libc_aout` and `distrib/rp2040/boardlibc-members` are unchanged, so
   programs compiled on the board cannot call them.
3. **Not provided.** Wide streams and `wcsftime`; `<complex.h>`,
   `<threads.h>` and `<stdatomic.h>`, without the `__STDC_NO_*__` macros that
   would announce their absence.
4. **NetBSD findings.** Five NetBSD libm defects (`rint`, `rintf`, `logb`,
   `logbf`, `fma`) are recorded in `tests/libc_contracts/netbsd_oracle.py`
   and the closure record; no report has been sent anywhere.
5. **Index row.** `docs/INDEX.md` does not yet list this file.

## Unrun here

Board execution; `check-renode`; the complete `logged-ilp32` inventory and
`check-test-execution`, both stopped by this container's process-group
cleanup (status 125, identical on the base commit); and
`check-repository-coverage`, which needs history the shallow clone lacked.
