#ifndef BNSHIM_ERR_H
#define BNSHIM_ERR_H
static inline void ERR_load_BN_strings(void) {}
static inline unsigned long ERR_get_error(void) { return 0; }
static inline const char *ERR_reason_error_string(unsigned long e) { (void)e; return "bignum error"; }
#endif
