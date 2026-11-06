/*
 * The err(3) family for the newlib build of dc under qemu-arm; newlib ships
 * no <err.h>. The implementations are in qemu_shim.c.
 */

#ifndef DCBC_QEMU_ERR_H
#define DCBC_QEMU_ERR_H

void err(int, const char *, ...) __attribute__((__noreturn__));
void errx(int, const char *, ...) __attribute__((__noreturn__));
void warn(const char *, ...);
void warnx(const char *, ...);

#endif /* DCBC_QEMU_ERR_H */
