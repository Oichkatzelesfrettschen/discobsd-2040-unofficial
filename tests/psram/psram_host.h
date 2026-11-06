/*
 * Register, pin, delay and interrupt hooks that let psram_xfer.c run on the
 * host against the simulator in pio_sim.py. psram_host.c forwards each call
 * to a function the Python harness registers through ctypes.
 */
#ifndef PSRAM_HOST_H
#define PSRAM_HOST_H

unsigned int psram_host_rd(unsigned int off);
void psram_host_wr(unsigned int off, unsigned int v);
void psram_host_unreset(void);
void psram_host_pin(unsigned int n, unsigned int pad);
void psram_host_delay_us(unsigned int us);
int psram_host_irq_save(void);
void psram_host_irq_restore(int s);

#define	PSRAM_RD(off)		psram_host_rd(off)
#define	PSRAM_WR(off, v)	psram_host_wr((off), (v))
#define	PSRAM_UNRESET()		psram_host_unreset()
#define	PSRAM_PIN(n, pad)	psram_host_pin((n), (pad))
#define	PSRAM_DELAY_US(us)	psram_host_delay_us(us)
#define	PSRAM_IRQ_SAVE()	psram_host_irq_save()
#define	PSRAM_IRQ_RESTORE(s)	psram_host_irq_restore(s)
#define	PSRAM_RAMFUNC

#endif /* !PSRAM_HOST_H */
