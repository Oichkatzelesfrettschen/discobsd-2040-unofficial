/*
 * pdp11: a PDP-11/40 with KT11 segmentation, an RK11/RK05 and a KL11
 * console, small enough to run as a DiscoBSD process. The instruction
 * set and device model descend from Julius Schmidt's JavaScript PDP-11
 * by way of Dave Cheney's avr11 (WTFPL, see COPYING); the memory
 * boundary, the access-control checks, the RK error path and the host
 * back ends are this port's.
 *
 * Every guest address is an 18-bit physical address once the MMU has
 * decoded it. Memory ends at MEMSIZE; the I/O page starts at 0760000;
 * everything between is nonexistent and traps through vector 4.
 */
#ifndef PDP11_H
#define PDP11_H

#include <setjmp.h>
#include <stdint.h>

#ifndef MEMSIZE
#define MEMSIZE (64 * 1024)		/* bytes of guest core */
#endif
#define IOPAGE 0760000L

#ifndef SEEK_SET		/* the port's unistd.h spells it L_SET */
#define SEEK_SET 0
#endif

/* Trap and interrupt vectors. */
#define INTBUS		0004
#define INTINVAL	0010
#define INTBPT		0014
#define INTIOT		0020
#define INTEMT		0030
#define INTTRAP		0034
#define INTTTYIN	0060
#define INTTTYOUT	0064
#define INTCLOCK	0100
#define INTRK		0220
#define INTFAULT	0250

#define FLAGN 8
#define FLAGZ 4
#define FLAGV 2
#define FLAGC 1

/* Nonlocal exit out of the instruction in progress: main() catches it. */
extern jmp_buf trapbuf;
#define TRAP(vec) _longjmp(trapbuf, (vec))

/* cpu.c */
extern uint16_t R[8];
extern uint16_t PS, PC, KSP, USP, LKS;
extern int curuser, prevuser, waiting;
void cpu_reset(void);
void cpu_step(void);
void cpu_trapat(uint16_t vec);
void cpu_interrupt(uint8_t vec, uint8_t pri);
int cpu_intpending(void);
void cpu_handleinterrupt(void);
void cpu_printstate(void);

/* mmu.c */
extern uint16_t SR0, SR2;
uint32_t mmu_decode(uint16_t a, int w, int user);
uint16_t mmu_read16(uint32_t a);
void mmu_write16(uint32_t a, uint16_t v);
void mmu_reset(void);

/* unibus.c: CPU-side access that traps; DMA-side access that reports. */
extern uint16_t mem[MEMSIZE / 2];
uint16_t ub_read16(uint32_t a);
void ub_write16(uint32_t a, uint16_t v);
uint16_t ub_read8(uint32_t a);
void ub_write8(uint32_t a, uint16_t v);
int dma_read16(uint32_t a, uint16_t *v);
int dma_write16(uint32_t a, uint16_t v);

/* rk.c */
int rk_open(const char *path);
void rk_close(void);
void rk_reset(void);
uint16_t rk_read16(uint32_t a);
void rk_write16(uint32_t a, uint16_t v);

/* cons.c */
void cons_reset(void);
uint16_t cons_read16(uint32_t a);
void cons_write16(uint32_t a, uint16_t v);
void cons_poll(void);
int cons_open(void);
void cons_close(void);

/* main.c */
void fatal(const char *msg, uint32_t a);
void clock_poll(void);

#endif
