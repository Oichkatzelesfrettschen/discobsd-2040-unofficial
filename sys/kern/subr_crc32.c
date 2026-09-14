/*
 * CRC-32 one bit at a time: the packed executable loader checks every
 * expanded byte with it, and the 64-byte table a nibble-wise variant
 * wants is text the kernel does not spend until a measurement says the
 * time matters. The result agrees with zlib's crc32() when started from
 * CRC32_INIT and finished with CRC32_FIN.
 */
#include <sys/exec_hsaout.h>

unsigned
crc32_update (unsigned crc, const void *buf, unsigned len)
{
    const unsigned char *p = buf;
    int i;

    while (len-- != 0) {
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
    }
    return crc;
}
