/*
 * CRC-32 one nibble at a time: the packed executable loader checks every
 * expanded byte with it. crc32_nibble[i] is the effect of four reflected
 * shift steps of the polynomial 0xedb88320 on the value i, so a byte takes
 * two table lookups; the 64-byte const table lives in flash. The result
 * agrees with zlib's crc32() when started from CRC32_INIT and finished
 * with CRC32_FIN.
 */
#include <sys/exec_hsaout.h>

static const unsigned crc32_nibble[16] = {
    0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
    0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
    0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
    0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
};

unsigned
crc32_update (unsigned crc, const void *buf, unsigned len)
{
    const unsigned char *p = buf;

    while (len-- != 0) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc32_nibble[crc & 15];
        crc = (crc >> 4) ^ crc32_nibble[crc & 15];
    }
    return crc;
}
