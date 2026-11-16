/* The kernel and its runtime use ARMv6-M, Thumb-1 and the base PCS. */
#if !defined(__ARM_ARCH_6M__) || !defined(__thumb__) || !defined(__SOFTFP__)
#error RP2040 requires ARMv6-M Thumb soft-float code
#endif
#if !defined(__ARM_PCS) || __ARM_ARCH_ISA_THUMB != 1
#error RP2040 requires the base ARM PCS and Thumb-1
#endif
#ifdef __ARM_FP
#error RP2040 has no hardware floating-point unit
#endif

enum small_enum { small_enum_zero, small_enum_one };
_Static_assert(sizeof(enum small_enum) == 1, "kernel uses the runtime enum layout");

int
target_flag_probe(int first, int second)
{
    return first + second;
}
