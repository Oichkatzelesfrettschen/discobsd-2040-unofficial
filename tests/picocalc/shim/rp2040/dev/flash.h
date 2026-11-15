/* Host stand-in for sys/arch/rp2040/dev/flash.h, which sd.c includes. */
#ifndef _SHIM_RP2040_DEV_FLASH_H_
#define _SHIM_RP2040_DEV_FLASH_H_

/* Whether the PicoCalc loader's stamp is present; the test sets it. */
int	flash_loader_started(void);

#endif
