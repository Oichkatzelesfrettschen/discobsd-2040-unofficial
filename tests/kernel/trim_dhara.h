/*
 * The interface between trim_order_test.c (kernel headers) and
 * trim_dhara.c (host headers): plain types only, so neither side sees a
 * type whose width the other defines differently.
 */
#ifndef TRIM_DHARA_H
#define TRIM_DHARA_H

void td_erase(void);
void td_reset(void);
int td_read(unsigned sector, unsigned char *data);
int td_write(unsigned sector, const unsigned char *data);
int td_trim(unsigned sector);
int td_sync(void);
unsigned td_live(void);
unsigned td_faults(void);
unsigned td_pages(void);
void td_save(void);
void td_restore(void);
int td_load(const char *path, unsigned *units, unsigned *skipped);

#endif
