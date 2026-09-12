/*
 * Route the AEABI single- and double-precision floating-point arithmetic
 * through the RP2040 bootrom's floating-point library. Every RP2040 carries
 * a correctly-rounded IEEE single-precision library in its 16 KB mask ROM,
 * and bootrom V2 and later add a double-precision one (datasheet 2.8.3.2);
 * these routines run several times faster than the libgcc soft-float this
 * would otherwise link and cost no flash beyond this shim, since the code
 * lives in silicon. See sys/arch/rp2040/doc/research/float-libs.md.
 *
 * The shim owns the __aeabi arithmetic symbols through the linker's
 * --wrap (share/mk/sys.mk sets -Wl,--wrap for rp2040 only): a reference to
 * __aeabi_fadd resolves here as __wrap___aeabi_fadd, and __real___aeabi_*
 * names the original libgcc routine. Single precision is present on every
 * bootrom version, so its ops always take the ROM path and never reference
 * __real, which keeps libgcc's single-precision objects out of the link.
 * Double precision falls back to __real (libgcc) on a bootrom V1 part,
 * whose data table has no double entry.
 *
 * Conformance follows the ROM (datasheet 2.8.3.2.1): input denormals are
 * treated as zero, input NaNs as infinities, output denormals flushed to
 * zero, output NaNs rendered as infinities, and only round-to-nearest-even
 * is supported. The five basic ops are correctly rounded within that.
 * Division goes through the ROM as well: the ROM divide uses the SIO
 * hardware divider, and locore.S's setjmp/longjmp checkpoint that divider
 * across a context switch, so a process preempted between the divisor
 * write and the quotient read keeps its own result -- which makes the ROM
 * divide safe under preemption.
 *
 * The ROM entries use the soft-float AAPCS -- a float in r0 (and r1), a
 * double in r0:r1 (and r2:r3) -- the same registers the __aeabi symbols
 * use, so each wrapper is a direct call through the resolved pointer.
 */

/* Bootrom lookup, datasheet 2.8.3.1: a 16-bit pointer to rom_data_table at
 * address 0x16, a 16-bit pointer to the rom_table_lookup helper at 0x18.
 * A 2-char code is packed c1 | (c2 << 8). The float function tables are
 * reached through the data table by code 'S','F' (single) and 'S','D'
 * (double, absent on bootrom V1). */
static void *
rom_data_lookup(unsigned code)
{
	/* Fixed ROM addresses, read as real memory. The array-bounds
	 * heuristic mistakes the small constant addresses for near-null
	 * object accesses; they are neither, so it is silenced here. */
	volatile unsigned short *lookup_at = (volatile unsigned short *)0x18;
	volatile unsigned short *data_at   = (volatile unsigned short *)0x16;
	void *(*lookup)(unsigned short *, unsigned);
	unsigned short *data_table;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
	lookup = (void *(*)(unsigned short *, unsigned))(unsigned)*lookup_at;
	data_table = (unsigned short *)(unsigned)*data_at;
#pragma GCC diagnostic pop
	return lookup(data_table, code);
}

/* soft_float_table and soft_double_table, each an array of function
 * pointers at the fixed order in the bootrom (bootrom_rt0.S): index 0 add,
 * 1 sub, 2 mul. Resolved once; sd_tab stays null on a V1 part. */
static void **sf_tab;
static void **sd_tab;
static int resolved;

static void
resolve(void)
{
	sf_tab = (void **)rom_data_lookup('S' | ('F' << 8));
	sd_tab = (void **)rom_data_lookup('S' | ('D' << 8));
	resolved = 1;
}

typedef float  (*romff)(float, float);
typedef double (*romdd)(double, double);

/* libgcc originals, reached through --wrap; referenced only by the double
 * fallback, so libgcc's single-precision ops are never pulled in. */
extern double __real___aeabi_dadd(double, double);
extern double __real___aeabi_dsub(double, double);
extern double __real___aeabi_dmul(double, double);
extern double __real___aeabi_ddiv(double, double);

float
__wrap___aeabi_fadd(float a, float b)
{
	if (!resolved)
		resolve();
	return ((romff)sf_tab[0])(a, b);
}

float
__wrap___aeabi_fsub(float a, float b)
{
	if (!resolved)
		resolve();
	return ((romff)sf_tab[1])(a, b);
}

float
__wrap___aeabi_fmul(float a, float b)
{
	if (!resolved)
		resolve();
	return ((romff)sf_tab[2])(a, b);
}

/* Division (index 3) uses the SIO hardware divider; locore.S checkpoints
 * the divider across a context switch, so the ROM path is safe under
 * preemption. */
float
__wrap___aeabi_fdiv(float a, float b)
{
	if (!resolved)
		resolve();
	return ((romff)sf_tab[3])(a, b);
}

double
__wrap___aeabi_dadd(double a, double b)
{
	if (!resolved)
		resolve();
	return sd_tab ? ((romdd)sd_tab[0])(a, b) : __real___aeabi_dadd(a, b);
}

double
__wrap___aeabi_dsub(double a, double b)
{
	if (!resolved)
		resolve();
	return sd_tab ? ((romdd)sd_tab[1])(a, b) : __real___aeabi_dsub(a, b);
}

double
__wrap___aeabi_dmul(double a, double b)
{
	if (!resolved)
		resolve();
	return sd_tab ? ((romdd)sd_tab[2])(a, b) : __real___aeabi_dmul(a, b);
}

double
__wrap___aeabi_ddiv(double a, double b)
{
	if (!resolved)
		resolve();
	return sd_tab ? ((romdd)sd_tab[3])(a, b) : __real___aeabi_ddiv(a, b);
}
