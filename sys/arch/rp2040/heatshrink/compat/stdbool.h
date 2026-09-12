/*
 * heatshrink_encoder.c names <stdbool.h> and a -nostdinc kernel has none.
 * C99 specifies exactly these four macros, so the vendored source stays
 * byte-identical to upstream and can be re-fetched without an edit.
 */

#ifndef	_HEATSHRINK_COMPAT_STDBOOL_H_
#define	_HEATSHRINK_COMPAT_STDBOOL_H_

#define	bool			_Bool
#define	true			1
#define	false			0
#define	__bool_true_false_are_defined 1

#endif	/* !_HEATSHRINK_COMPAT_STDBOOL_H_ */
