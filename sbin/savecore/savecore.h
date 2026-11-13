/*
 * The system interfaces savecore.c calls, which sysdep.c implements on the
 * RP2040 and tests/picocalc/dump_test.c replaces on the host.
 */
#ifndef _SAVECORE_H_
#define _SAVECORE_H_

/* Name the raw device of the SD card's dump partition; 0 or -1. */
int		dump_device(char *path, unsigned size);

/* Bytes the opened device or file holds; -1 when unknown. */
long long	media_bytes(int fd);

/* Bytes an ordinary user may still allocate under dir; -1 on error. */
long long	free_bytes(const char *dir);

/* machdep.image_crc of the running kernel; 0 or -1. */
int		running_image_crc(unsigned *crc);

#endif
