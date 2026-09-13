/*
 * Alignment probes for t17_align.c, compiled by the cross compiler because
 * smlrc reads no inline assembly.
 *
 * Each probe is naked, so the first instruction runs with the caller's SP
 * still in place and "mov r0, sp" reads exactly the value AAPCS32 5.2.1.2
 * constrains at a public interface. The probe returns sp & 7, which is zero
 * for a conforming call site. Parameters exist only in the declarations the
 * smlrc-compiled caller uses; a naked body reads none of them, so one body
 * serves every signature.
 */

#define PROBE(name)							\
__attribute__((naked)) int name(void)					\
{									\
	__asm__ volatile(".syntax unified\n\t"				\
			 "mov r0, sp\n\t"				\
			 "movs r1, #7\n\t"				\
			 "ands r0, r0, r1\n\t"				\
			 "bx lr");					\
}

PROBE(p0)
PROBE(p1)
PROBE(p2)
PROBE(p3)
PROBE(p4)
PROBE(p5)
PROBE(p6)
PROBE(p7)
PROBE(p8)
PROBE(p9)
PROBE(p10)
PROBE(pv)
PROBE(ps)
PROBE(pso)
PROBE(psm)
