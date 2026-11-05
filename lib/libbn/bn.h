/*
 * libbn -- the arbitrary-precision integer subset that OpenBSD dc(1) and
 * bc(1) use, with the signatures and return conventions of the BN_*
 * interface documented in BN_add(3), BN_add_word(3), BN_bn2bin(3),
 * BN_cmp(3), BN_copy(3), BN_CTX_new(3), BN_new(3), BN_num_bytes(3),
 * BN_set_bit(3), BN_set_negative(3) and BN_zero(3). The implementation is
 * written from those manual pages and from textbook algorithms; no
 * OpenSSL or LibreSSL source is consulted.
 *
 * Conventions shared by every function:
 *
 *   - A value is a sign and a magnitude. Zero is never negative.
 *   - A function returning int yields 1 on success and 0 on failure
 *     (allocation failure, a size beyond the library's limit, or an
 *     argument outside the documented domain). A failed call leaves its
 *     destination numerically unchanged.
 *   - A result BIGNUM may alias an operand of BN_add, BN_sub, BN_mul,
 *     BN_div, BN_mod, BN_exp, BN_rshift and BN_rshift1.
 *   - Word operations act on the magnitude: BN_get_word reports |a|,
 *     BN_div_word truncates toward zero and returns a non-negative
 *     remainder, and BN_rshift shifts the magnitude and keeps the sign.
 *   - A BN_CTX holds one reusable scratch area. It may be NULL, in which
 *     case each call allocates and frees its own scratch.
 *   - No error queue exists; there is no ERR_get_error equivalent.
 */

#ifndef DISCOBSD_BN_H
#define DISCOBSD_BN_H

#include <stddef.h>
#include <stdint.h>

/* 32 bits on every machine, the width OpenBSD gives BN_ULONG on ILP32. */
typedef uint32_t BN_ULONG;

typedef struct bignum_st BIGNUM;
typedef struct bignum_ctx BN_CTX;

BN_CTX *BN_CTX_new(void);
void BN_CTX_free(BN_CTX *);

BIGNUM *BN_new(void);
void BN_free(BIGNUM *);
BIGNUM *BN_dup(const BIGNUM *);
BIGNUM *BN_copy(BIGNUM *, const BIGNUM *);

int BN_set_word(BIGNUM *, BN_ULONG);
BN_ULONG BN_get_word(const BIGNUM *);
int BN_one(BIGNUM *);

int BN_is_zero(const BIGNUM *);
int BN_is_one(const BIGNUM *);
int BN_is_negative(const BIGNUM *);
void BN_set_negative(BIGNUM *, int);

int BN_cmp(const BIGNUM *, const BIGNUM *);
int BN_ucmp(const BIGNUM *, const BIGNUM *);

int BN_num_bits(const BIGNUM *);
#define BN_num_bytes(a) ((BN_num_bits(a) + 7) / 8)
int BN_is_bit_set(const BIGNUM *, int);
int BN_mask_bits(BIGNUM *, int);
int BN_rshift(BIGNUM *, const BIGNUM *, int);
int BN_rshift1(BIGNUM *, const BIGNUM *);

int BN_add(BIGNUM *, const BIGNUM *, const BIGNUM *);
int BN_sub(BIGNUM *, const BIGNUM *, const BIGNUM *);
int BN_mul(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
int BN_div(BIGNUM *, BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);
#define BN_mod(rem, a, m, ctx) BN_div(NULL, (rem), (a), (m), (ctx))
int BN_exp(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);

int BN_add_word(BIGNUM *, BN_ULONG);
int BN_mul_word(BIGNUM *, BN_ULONG);
BN_ULONG BN_div_word(BIGNUM *, BN_ULONG);

char *BN_bn2dec(const BIGNUM *);
char *BN_bn2hex(const BIGNUM *);

#endif /* DISCOBSD_BN_H */
