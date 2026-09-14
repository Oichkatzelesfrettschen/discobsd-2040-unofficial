#ifndef LIBHSAOUT_H
#define LIBHSAOUT_H

#include <stddef.h>

/*
 * Pack a raw OMAGIC a.out into the container sys/sys/exec_hsaout.h
 * describes, and unpack one back into a raw image. Both return a malloc'd
 * buffer and its length, or NULL with *err naming the failure. hsaout_pack
 * expands its own output with the kernel's stream loop and compares it
 * with the input before returning, so a container it returns is one the
 * loader reproduces.
 */
unsigned char *hsaout_pack (const unsigned char *raw, size_t rawlen,
    size_t *outlen, const char **err);
unsigned char *hsaout_unpack (const unsigned char *packed, size_t len,
    size_t *outlen, const char **err);

/* Root file system blocks (1 KB, with indirect blocks) a file of len bytes takes. */
unsigned long hsaout_blocks (unsigned long len);
#endif
