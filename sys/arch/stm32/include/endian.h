
/*
 * The Cortex-M core runs little-endian; <sys/endian.h> derives the public
 * constants and conversions from _BYTE_ORDER.
 */
#ifndef _MACHINE_ENDIAN_H_
#define _MACHINE_ENDIAN_H_

#define _BYTE_ORDER	_LITTLE_ENDIAN

#include <sys/endian.h>

#endif /* !_MACHINE_ENDIAN_H_ */
