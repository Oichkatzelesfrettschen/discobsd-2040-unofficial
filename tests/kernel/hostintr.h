/*
 * Host stand-in for the port's interrupt priority entry points.
 *
 * sys/arch/rp2040/include/intr.h reaches PRIMASK through ARM inline assembly,
 * which no host assembler accepts. The tier's generated <machine/intr.h>
 * includes the port's header first, so every constant, the NVIC addresses and
 * IPLTOREG still come from the one place that defines them, and then includes
 * this file, which redefines the six spl macros. The port's own inline
 * functions survive as static inlines that nothing calls, so no assembly is
 * emitted.
 *
 * The stand-in counts rather than discards. hk_ipl carries the level the way
 * PRIMASK does, and hk_ipl_raises counts the raises, so a gate can assert that
 * a routine leaves the level where it found it. A clist routine returning with
 * interrupts still masked would wedge the board's console, and that is a
 * property worth stating rather than a side effect of the shim.
 */
#ifndef HOSTINTR_H
#define HOSTINTR_H

/* Defined by hostkern_kern.c; a gate reads them and resets them. */
extern int hk_ipl;
extern unsigned hk_ipl_raises;

static inline int
hk_intr_disable(void)
{
	int previous = hk_ipl;

	hk_ipl = 1;
	hk_ipl_raises++;
	return previous;
}

static inline int
hk_intr_enable(void)
{
	int previous = hk_ipl;

	hk_ipl = 0;
	return previous;
}

static inline void
hk_intr_restore(int previous)
{
	hk_ipl = previous;
}

#undef splhigh
#undef splclock
#undef spltty
#undef splnet
#undef splbio
#undef splsoftclock
#undef splx
#undef spl0

#define	splhigh()	hk_intr_disable()
#define	splclock()	hk_intr_disable()
#define	spltty()	hk_intr_disable()
#define	splnet()	hk_intr_disable()
#define	splbio()	hk_intr_disable()

#define	splsoftclock()	hk_intr_enable()

#define	splx(s)		hk_intr_restore(s)

#define	spl0()		hk_intr_enable()

#endif /* HOSTINTR_H */
