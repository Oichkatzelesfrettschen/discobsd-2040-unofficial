/*
 * calc -- scientific calculator for the PicoCalc console and any ANSI
 * terminal.
 *
 * Three modes share one line interface. Algebraic mode evaluates infix
 * expressions over double: + - * / % ^ !, unary minus, parentheses, the
 * functions of apply(), the constants of konsts[], ans, ten memory registers
 * m0..m9 and named variables. RPN mode (stack of STACKMAX values) reads
 * whitespace separated tokens. Programmer mode evaluates C-style integer
 * expressions over an 8, 16 or 32 bit word and prints each result in hex,
 * decimal, octal and binary.
 *
 * Numeric contract. Every operation that produces a non-finite value reports
 * "overflow"; an argument outside a function's domain reports "domain error";
 * a zero divisor reports "division by zero". An error leaves the session,
 * the variables and the RPN stack as they were before the failing operation.
 * Nesting of parentheses, function calls, prefix signs and exponent chains is
 * bounded by MAXDEPTH and reported as "nested too deeply". The boot ROM
 * double routines on the target return an infinity where IEEE arithmetic
 * returns a NaN (lib/libcalc/mathematics.h), so every result is tested with
 * fin() and the divisor, logarithm and root domains are checked before the
 * operation.
 *
 * Statistics, hyperbolic functions, log10 and rounding come from libcalc.
 * Unit factors are NIST SP 811 (2008), Appendix B.8, and the exact
 * definitions of the units named there. Constants are the CODATA 2022
 * recommended values published by NIST (physics.nist.gov/cuu/Constants); the
 * exact SI defining constants carry their defined values.
 *
 * Terminal mode. When stdin and stdout are both terminals the program reads
 * keys one byte at a time (CBREAK on the target, non-canonical termios on a
 * host), keeps the interrupt and terminate signals, restricts scrolling to
 * all rows but the last, and draws an item bar on the last row. F1 to F10,
 * as sent by the PicoCalc keyboard (sys/arch/rp2040/dev/picokbd_map.c) and by
 * an xterm, run the commands named in fkeys[]. The terminal is restored on
 * normal exit, end of input and on SIGINT, SIGQUIT, SIGHUP and SIGTERM.
 * Otherwise (a pipe, a file, -l, or expression arguments) the program is
 * line oriented and writes no escape sequences. CALC_NO_TTY removes the
 * terminal code, which the qemu-user build of the line interface uses.
 */

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mathematics.h"

#ifndef CALC_NO_TTY
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HOSTBUILD
#include <termios.h>
#else
#include <sgtty.h>
#endif
#endif

#define LINEMAX		160	/* longest input line, with its terminator */
#define MAXDEPTH	16	/* nesting bound of the expression grammar */
#define MAXARGS		32	/* arguments of one function call */
#define STATMAX		64	/* values of one stat or reg list */
#define STACKMAX	16	/* RPN stack levels */
#define NVARS		16	/* named variables */
#define NMEM		10	/* memory registers m0..m9 */
#define VNAME		10	/* longest identifier */
#define SCREEN_COLS	53	/* PicoCalc console width */
#define SCREEN_ROWS	32	/* PicoCalc console height */

enum { M_ALG, M_RPN, M_PROG };

typedef uint32_t word;

static int mode = M_ALG;
static int degmode;		/* trigonometry in degrees */
static int digits = 15;		/* significant digits printed */
static int bits = 32;		/* programmer word size */
static int quitting;
static int had_error;
static double ans;
static double mem[NMEM];
static double stack[STACKMAX];
static int sp;

static struct var {
	char name[VNAME + 1];
	double v;
} vars[NVARS];
static int nvars;

static jmp_buf errjmp;
static char errbuf[64];
static const char *errmsg;
static const char *src;		/* scan position */
static const char *line0;	/* start of the text being scanned */
static int depth;

/* ------------------------------------------------------------------ */
/* Errors                                                             */
/* ------------------------------------------------------------------ */

static _Noreturn void
fail(const char *msg)
{
	errmsg = msg;
	longjmp(errjmp, 1);
	for (;;)	/* longjmp never returns; the tree's setjmp.h omits noreturn */
		;
}

static _Noreturn void
failname(const char *msg, const char *name)
{
	snprintf(errbuf, sizeof errbuf, "%s: %s", msg, name);
	fail(errbuf);
}

static _Noreturn void
syntax(void)
{
	snprintf(errbuf, sizeof errbuf, "syntax error at column %d",
	    (int)(src - line0) + 1);
	fail(errbuf);
}

static int
fin(double x)
{
	return x - x == 0.0;
}

static double
chk(double x)
{
	if (!fin(x))
		fail("overflow");
	return x;
}

static void
cstatus(calc_status s)
{
	switch (s) {
	case CALC_OK:
		return;
	case CALC_E_SIZE:
		fail("not enough values");
	case CALC_E_DOMAIN:
		fail("domain error");
	case CALC_E_RANGE:
		fail("overflow");
	default:
		fail("bad argument");
	}
}

/* ------------------------------------------------------------------ */
/* Constants, functions and units                                     */
/* ------------------------------------------------------------------ */

static const struct konst {
	const char *name;
	double v;
} konsts[] = {
	{ "pi",	3.14159265358979323846 },
	{ "e",	2.71828182845904523536 },
	{ "c0",	299792458.0 },			/* speed of light, m/s */
	{ "planck", 6.62607015e-34 },		/* h, J s */
	{ "hbar", 1.0545718176461565e-34 },	/* h / 2 pi, J s */
	{ "qe",	1.602176634e-19 },		/* elementary charge, C */
	{ "kB",	1.380649e-23 },			/* Boltzmann, J/K */
	{ "NA",	6.02214076e23 },		/* Avogadro, 1/mol */
	{ "Rgas", 8.31446261815324 },		/* molar gas, J/(mol K) */
	{ "sigsb", 5.670374419e-8 },		/* Stefan-Boltzmann, W/(m2 K4) */
	{ "g0",	9.80665 },			/* standard gravity, m/s2 */
	{ "Gn",	6.6743e-11 },			/* gravitation, m3/(kg s2) */
	{ "me",	9.1093837139e-31 },		/* electron mass, kg */
	{ "mp",	1.67262192595e-27 },		/* proton mass, kg */
	{ "eps0", 8.8541878188e-12 },		/* vacuum permittivity, F/m */
	{ "mu0", 1.25663706127e-6 },		/* vacuum permeability, N/A2 */
	{ "alpha", 7.2973525643e-3 },		/* fine-structure constant */
};
#define NKONST ((int)(sizeof konsts / sizeof konsts[0]))

enum {
	F_SIN = 1, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_ATAN2,
	F_SINH, F_COSH, F_TANH, F_ASINH, F_ACOSH, F_ATANH,
	F_EXP, F_LN, F_LOG10, F_LOG2, F_SQRT, F_CBRT,
	F_ABS, F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_FRAC, F_SIGN,
	F_POW, F_MOD, F_HYPOT, F_FACT, F_TORAD, F_TODEG,
	F_SUM, F_MEAN, F_MEDIAN, F_MIN, F_MAX,
	F_STDEV, F_STDEVP, F_VAR, F_VARP
};

static const struct func {
	char name[8];
	unsigned char id, min, max;
} funcs[] = {
	{ "sin", F_SIN, 1, 1 }, { "cos", F_COS, 1, 1 }, { "tan", F_TAN, 1, 1 },
	{ "asin", F_ASIN, 1, 1 }, { "acos", F_ACOS, 1, 1 },
	{ "atan", F_ATAN, 1, 1 }, { "atan2", F_ATAN2, 2, 2 },
	{ "sinh", F_SINH, 1, 1 }, { "cosh", F_COSH, 1, 1 },
	{ "tanh", F_TANH, 1, 1 }, { "asinh", F_ASINH, 1, 1 },
	{ "acosh", F_ACOSH, 1, 1 }, { "atanh", F_ATANH, 1, 1 },
	{ "exp", F_EXP, 1, 1 }, { "ln", F_LN, 1, 1 },
	{ "log10", F_LOG10, 1, 1 }, { "log2", F_LOG2, 1, 1 },
	{ "sqrt", F_SQRT, 1, 1 }, { "cbrt", F_CBRT, 1, 1 },
	{ "abs", F_ABS, 1, 1 }, { "floor", F_FLOOR, 1, 1 },
	{ "ceil", F_CEIL, 1, 1 }, { "round", F_ROUND, 1, 1 },
	{ "trunc", F_TRUNC, 1, 1 }, { "frac", F_FRAC, 1, 1 },
	{ "sign", F_SIGN, 1, 1 },
	{ "pow", F_POW, 2, 2 }, { "mod", F_MOD, 2, 2 },
	{ "hypot", F_HYPOT, 2, 2 }, { "fact", F_FACT, 1, 1 },
	{ "torad", F_TORAD, 1, 1 }, { "todeg", F_TODEG, 1, 1 },
	{ "sum", F_SUM, 1, MAXARGS }, { "mean", F_MEAN, 1, MAXARGS },
	{ "median", F_MEDIAN, 1, MAXARGS }, { "min", F_MIN, 1, MAXARGS },
	{ "max", F_MAX, 1, MAXARGS }, { "stdev", F_STDEV, 1, MAXARGS },
	{ "stdevp", F_STDEVP, 1, MAXARGS }, { "var", F_VAR, 1, MAXARGS },
	{ "varp", F_VARP, 1, MAXARGS },
};
#define NFUNC ((int)(sizeof funcs / sizeof funcs[0]))

static const struct func *
findfunc(const char *name)
{
	int i;

	for (i = 0; i < NFUNC; i++)
		if (strcmp(funcs[i].name, name) == 0)
			return &funcs[i];
	return NULL;
}

/*
 * Unit table: SI value = v * f + off. The factors are the exact values of
 * NIST SP 811 Appendix B.8 or the exact definitions behind its rounded
 * entries (international foot, avoirdupois pound, US liquid gallon,
 * thermochemical calorie, IT British thermal unit, conventional mercury).
 */
enum { D_LEN, D_MASS, D_VOL, D_AREA, D_PRESS, D_ENERGY, D_POWER, D_SPEED,
    D_FORCE, D_TIME, D_ANGLE, D_TEMP };

static const char *const dimnames[] = {
	"length", "mass", "volume", "area", "pressure", "energy", "power",
	"speed", "force", "time", "angle", "temperature"
};

#define PI_ 3.14159265358979323846

static const struct unit {
	char name[7];
	unsigned char dim;
	double f, off;
} units[] = {
	{ "m", D_LEN, 1.0, 0 }, { "km", D_LEN, 1000.0, 0 },
	{ "cm", D_LEN, 0.01, 0 }, { "mm", D_LEN, 0.001, 0 },
	{ "um", D_LEN, 1e-6, 0 }, { "in", D_LEN, 0.0254, 0 },
	{ "ft", D_LEN, 0.3048, 0 }, { "yd", D_LEN, 0.9144, 0 },
	{ "mi", D_LEN, 1609.344, 0 }, { "nmi", D_LEN, 1852.0, 0 },
	{ "kg", D_MASS, 1.0, 0 }, { "g", D_MASS, 0.001, 0 },
	{ "mg", D_MASS, 1e-6, 0 }, { "t", D_MASS, 1000.0, 0 },
	{ "lb", D_MASS, 0.45359237, 0 }, { "oz", D_MASS, 0.028349523125, 0 },
	{ "L", D_VOL, 0.001, 0 }, { "mL", D_VOL, 1e-6, 0 },
	{ "m3", D_VOL, 1.0, 0 }, { "galUS", D_VOL, 3.785411784e-3, 0 },
	{ "qtUS", D_VOL, 9.46352946e-4, 0 }, { "ptUS", D_VOL, 4.73176473e-4, 0 },
	{ "cupUS", D_VOL, 2.365882365e-4, 0 },
	{ "flozUS", D_VOL, 2.95735295625e-5, 0 },
	{ "galUK", D_VOL, 4.54609e-3, 0 },
	{ "m2", D_AREA, 1.0, 0 }, { "cm2", D_AREA, 1e-4, 0 },
	{ "km2", D_AREA, 1e6, 0 }, { "ha", D_AREA, 1e4, 0 },
	{ "in2", D_AREA, 6.4516e-4, 0 }, { "ft2", D_AREA, 0.09290304, 0 },
	{ "acre", D_AREA, 4046.8564224, 0 }, { "mi2", D_AREA, 2589988.110336, 0 },
	{ "Pa", D_PRESS, 1.0, 0 }, { "kPa", D_PRESS, 1000.0, 0 },
	{ "bar", D_PRESS, 1e5, 0 }, { "atm", D_PRESS, 101325.0, 0 },
	{ "psi", D_PRESS, 6894.757293168361, 0 },
	{ "torr", D_PRESS, 101325.0 / 760.0, 0 },
	{ "mmHg", D_PRESS, 133.322387415, 0 },
	{ "J", D_ENERGY, 1.0, 0 }, { "kJ", D_ENERGY, 1000.0, 0 },
	{ "cal", D_ENERGY, 4.184, 0 }, { "kcal", D_ENERGY, 4184.0, 0 },
	{ "Wh", D_ENERGY, 3600.0, 0 }, { "kWh", D_ENERGY, 3.6e6, 0 },
	{ "BTU", D_ENERGY, 1055.05585262, 0 }, { "eV", D_ENERGY, 1.602176634e-19, 0 },
	{ "ftlbf", D_ENERGY, 1.3558179483314006, 0 },
	{ "W", D_POWER, 1.0, 0 }, { "kW", D_POWER, 1000.0, 0 },
	{ "hp", D_POWER, 745.6998715822702, 0 },
	{ "m/s", D_SPEED, 1.0, 0 }, { "km/h", D_SPEED, 1.0 / 3.6, 0 },
	{ "mph", D_SPEED, 0.44704, 0 }, { "kn", D_SPEED, 1852.0 / 3600.0, 0 },
	{ "ft/s", D_SPEED, 0.3048, 0 },
	{ "N", D_FORCE, 1.0, 0 }, { "lbf", D_FORCE, 4.4482216152605, 0 },
	{ "kgf", D_FORCE, 9.80665, 0 },
	{ "s", D_TIME, 1.0, 0 }, { "ms", D_TIME, 0.001, 0 },
	{ "min", D_TIME, 60.0, 0 }, { "h", D_TIME, 3600.0, 0 },
	{ "d", D_TIME, 86400.0, 0 },
	{ "rad", D_ANGLE, 1.0, 0 }, { "deg", D_ANGLE, PI_ / 180.0, 0 },
	{ "grad", D_ANGLE, PI_ / 200.0, 0 }, { "turn", D_ANGLE, 2.0 * PI_, 0 },
	{ "K", D_TEMP, 1.0, 0 }, { "degC", D_TEMP, 1.0, 273.15 },
	{ "degF", D_TEMP, 5.0 / 9.0, 459.67 * 5.0 / 9.0 },
	{ "degR", D_TEMP, 5.0 / 9.0, 0 },
};
#define NUNIT ((int)(sizeof units / sizeof units[0]))

static const struct unit *
findunit(const char *name)
{
	int i;

	for (i = 0; i < NUNIT; i++)
		if (strcmp(units[i].name, name) == 0)
			return &units[i];
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                         */
/* ------------------------------------------------------------------ */

static double
ipow(double a, long n)
{
	double r = 1.0;

	while (n > 0) {
		if (n & 1)
			r = chk(r * a);
		n >>= 1;
		if (n > 0)
			a = chk(a * a);
	}
	return r;
}

static double
dpow(double a, double b)
{
	if (b == floor(b) && b >= -1024.0 && b <= 1024.0) {
		long n = (long)b;

		if (n >= 0)
			return ipow(a, n);
		if (a == 0.0)
			fail("division by zero");
		return ipow(chk(1.0 / a), -n);
	}
	if (a < 0.0)
		fail("domain error");
	if (a == 0.0) {
		if (b < 0.0)
			fail("division by zero");
		return 0.0;
	}
	return chk(pow(a, b));
}

static double
arith(int op, double a, double b)
{
	switch (op) {
	case '+':
		return chk(a + b);
	case '-':
		return chk(a - b);
	case '*':
		return chk(a * b);
	case '/':
		if (b == 0.0)
			fail("division by zero");
		return chk(a / b);
	case '%':
		if (b == 0.0)
			fail("division by zero");
		return chk(fmod(a, b));
	default:
		return dpow(a, b);
	}
}

static double
factorial(double x)
{
	double r = 1.0;
	int i, n;

	if (x < 0.0 || x > 170.0 || x != floor(x))
		fail("domain error");
	n = (int)x;
	for (i = 2; i <= n; i++)
		r *= (double)i;
	return r;
}

static double
log2x(double x)
{
	double r, n, p = 1.0;
	int i, k;

	if (x <= 0.0)
		fail("domain error");
	r = log(x) / 0.693147180559945309417;
	n = floor(r + 0.5);
	if (n >= -1022.0 && n <= 1023.0) {
		k = (int)(n < 0.0 ? -n : n);
		for (i = 0; i < k; i++)
			p *= 2.0;
		if (n < 0.0)
			p = 1.0 / p;
		if (p == x)
			return n;
	}
	return r;
}

static double
cuberoot(double x)
{
	double a = x < 0.0 ? -x : x, r;

	if (a == 0.0)
		return 0.0;
	r = pow(a, 1.0 / 3.0);
	r -= (r * r * r - a) / (3.0 * r * r);
	return x < 0.0 ? -r : r;
}

/* Radians to the current angle unit. */
static double
ang_out(double r)
{
	return degmode ? calc_angle_from_rad(CALC_DEGREES, r) : r;
}

/*
 * Trigonometric function of x in the current angle unit. Degrees are
 * reduced to one turn first, and multiples of 90 give exact values.
 */
static double
trig(int id, double x)
{
	static const double sn[4] = { 0.0, 1.0, 0.0, -1.0 };
	static const double cs[4] = { 1.0, 0.0, -1.0, 0.0 };
	double r;
	int q;

	if (degmode) {
		r = fmod(x, 360.0);
		if (r < 0.0)
			r += 360.0;
		q = (int)(r / 90.0);
		if (r == 90.0 * (double)q) {
			if (id == F_TAN) {
				if (q & 1)
					fail("domain error");
				return 0.0;
			}
			return id == F_SIN ? sn[q & 3] : cs[q & 3];
		}
		x = calc_angle_to_rad(CALC_DEGREES, r);
	}
	if (id == F_SIN)
		return sin(x);
	if (id == F_COS)
		return cos(x);
	return tan(x);
}

static double
apply(int id, const double *a, int n)
{
	static double scratch[STATMAX];
	double r = 0.0, x = a[0];

	switch (id) {
	case F_SIN:
	case F_COS:
	case F_TAN:
		return chk(trig(id, x));
	case F_ASIN:
		if (x > 1.0 || x < -1.0)
			fail("domain error");
		return ang_out(atan2(x, sqrt((1.0 - x) * (1.0 + x))));
	case F_ACOS:
		if (x > 1.0 || x < -1.0)
			fail("domain error");
		return ang_out(2.0 * atan2(sqrt(1.0 - x), sqrt(1.0 + x)));
	case F_ATAN:
		return ang_out(atan(x));
	case F_ATAN2:
		if (a[0] == 0.0 && a[1] == 0.0)
			fail("domain error");
		return ang_out(atan2(a[0], a[1]));
	case F_SINH:
		cstatus(calc_sinh(x, &r));
		return chk(r);
	case F_COSH:
		cstatus(calc_cosh(x, &r));
		return chk(r);
	case F_TANH:
		cstatus(calc_tanh(x, &r));
		return r;
	case F_ASINH:
		return chk(calc_asinh(x));
	case F_ACOSH:
		if (x < 1.0)
			fail("domain error");
		return chk(calc_acosh(x));
	case F_ATANH:
		if (x <= -1.0 || x >= 1.0)
			fail("domain error");
		return chk(calc_atanh(x));
	case F_EXP:
		if (x > 709.782712893384)
			fail("overflow");
		return exp(x);
	case F_LN:
		if (x <= 0.0)
			fail("domain error");
		return log(x);
	case F_LOG10:
		cstatus(calc_log10(x, &r));
		return r;
	case F_LOG2:
		return log2x(x);
	case F_SQRT:
		if (x < 0.0)
			fail("domain error");
		return sqrt(x);
	case F_CBRT:
		return cuberoot(x);
	case F_ABS:
		return x < 0.0 ? -x : x;
	case F_FLOOR:
		return floor(x);
	case F_CEIL:
		return ceil(x);
	case F_ROUND:
		return calc_round(x);
	case F_TRUNC:
		return x < 0.0 ? ceil(x) : floor(x);
	case F_FRAC:
		return x - (x < 0.0 ? ceil(x) : floor(x));
	case F_SIGN:
		return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
	case F_POW:
		return dpow(a[0], a[1]);
	case F_MOD:
		return arith('%', a[0], a[1]);
	case F_HYPOT:
		return chk(hypot(a[0], a[1]));
	case F_FACT:
		return factorial(x);
	case F_TORAD:
		return calc_angle_to_rad(CALC_DEGREES, x);
	case F_TODEG:
		return calc_angle_from_rad(CALC_DEGREES, x);
	case F_SUM:
		cstatus(calc_sum(a, (size_t)n, &r));
		return chk(r);
	case F_MEAN:
		cstatus(calc_mean(a, (size_t)n, &r));
		return chk(r);
	case F_MEDIAN:
		cstatus(calc_median(a, (size_t)n, scratch, &r));
		return r;
	case F_MIN:
		cstatus(calc_min(a, (size_t)n, &r, NULL));
		return r;
	case F_MAX:
		cstatus(calc_max(a, (size_t)n, &r, NULL));
		return r;
	case F_STDEV:
		cstatus(calc_stddev(a, (size_t)n, 1, &r));
		return chk(r);
	case F_STDEVP:
		cstatus(calc_stddev(a, (size_t)n, 0, &r));
		return chk(r);
	case F_VAR:
		cstatus(calc_variance(a, (size_t)n, 1, &r));
		return chk(r);
	case F_VARP:
		cstatus(calc_variance(a, (size_t)n, 0, &r));
		return chk(r);
	}
	fail("bad function");
}

/* ------------------------------------------------------------------ */
/* Names                                                              */
/* ------------------------------------------------------------------ */

static int
lookup(const char *name, double *out)
{
	int i;

	if (name[0] == 'm' && isdigit((unsigned char)name[1]) && name[2] == '\0') {
		*out = mem[name[1] - '0'];
		return 1;
	}
	if (strcmp(name, "ans") == 0) {
		*out = ans;
		return 1;
	}
	for (i = 0; i < nvars; i++)
		if (strcmp(vars[i].name, name) == 0) {
			*out = vars[i].v;
			return 1;
		}
	for (i = 0; i < NKONST; i++)
		if (strcmp(konsts[i].name, name) == 0) {
			*out = konsts[i].v;
			return 1;
		}
	return 0;
}

enum {
	C_HELP, C_HELP2, C_QUIT, C_EXIT, C_DEG, C_RAD, C_ANGLE, C_ALG, C_RPN,
	C_PROG, C_BITS, C_DIGITS, C_CLR, C_CLEAR, C_VARS, C_CONST, C_UNITS,
	C_CONV, C_STAT, C_REG, C_STACK, C_COUNT
};

static const char *const cmdwords[C_COUNT + 1] = {
	[C_HELP] = "help", [C_HELP2] = "?", [C_QUIT] = "quit",
	[C_EXIT] = "exit", [C_DEG] = "deg", [C_RAD] = "rad",
	[C_ANGLE] = "angle", [C_ALG] = "alg", [C_RPN] = "rpn",
	[C_PROG] = "prog", [C_BITS] = "bits", [C_DIGITS] = "digits",
	[C_CLR] = "clr", [C_CLEAR] = "clear", [C_VARS] = "vars",
	[C_CONST] = "const", [C_UNITS] = "units", [C_CONV] = "conv",
	[C_STAT] = "stat", [C_REG] = "reg", [C_STACK] = "stack",
};

static int
reserved(const char *name)
{
	int i;

	if (findfunc(name) != NULL || strcmp(name, "ans") == 0)
		return 1;
	for (i = 0; i < NKONST; i++)
		if (strcmp(konsts[i].name, name) == 0)
			return 1;
	for (i = 0; i < C_COUNT; i++)
		if (strcmp(cmdwords[i], name) == 0)
			return 1;
	return 0;
}

static void
setvar(const char *name, double v)
{
	int i;

	if (name[0] == 'm' && isdigit((unsigned char)name[1]) && name[2] == '\0') {
		mem[name[1] - '0'] = v;
		return;
	}
	if (reserved(name))
		failname("cannot assign to", name);
	for (i = 0; i < nvars; i++)
		if (strcmp(vars[i].name, name) == 0) {
			vars[i].v = v;
			return;
		}
	if (nvars == NVARS)
		fail("too many variables");
	strcpy(vars[nvars].name, name);
	vars[nvars++].v = v;
}

/* ------------------------------------------------------------------ */
/* Algebraic expressions                                              */
/* ------------------------------------------------------------------ */

static int
peek(void)
{
	while (*src == ' ' || *src == '\t')
		src++;
	return (unsigned char)*src;
}

static void
ident(char *name)
{
	int n = 0;

	peek();
	while (isalnum((unsigned char)*src) || *src == '_') {
		if (n == VNAME)
			fail("name too long");
		name[n++] = *src++;
	}
	name[n] = '\0';
}

static int
digitval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 99;
}

/* A decimal, exponent or 0x, 0b, 0o literal at src. */
static double
number(void)
{
	char buf[32];
	const char *p = src;
	double v = 0.0;
	int base = 0, d, n = 0, ndig = 0;

	if (p[0] == '0') {
		if (p[1] == 'x' || p[1] == 'X')
			base = 16;
		else if (p[1] == 'b' || p[1] == 'B')
			base = 2;
		else if (p[1] == 'o' || p[1] == 'O')
			base = 8;
	}
	if (base != 0) {
		p += 2;
		while ((d = digitval((unsigned char)*p)) < base) {
			v = v * (double)base + (double)d;
			p++;
			ndig++;
		}
		if (ndig == 0) {
			src = p;
			syntax();
		}
		src = p;
		return chk(v);
	}
	while (isdigit((unsigned char)*p) || *p == '.') {
		if (*p != '.')
			ndig++;
		if (n == (int)sizeof buf - 1)
			fail("number too long");
		buf[n++] = *p++;
	}
	if (ndig == 0) {
		src = p;
		syntax();
	}
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;

		if (*q == '+' || *q == '-')
			q++;
		if (isdigit((unsigned char)*q))
			while (p < q || isdigit((unsigned char)*p)) {
				if (n == (int)sizeof buf - 1)
					fail("number too long");
				buf[n++] = *p++;
			}
	}
	buf[n] = '\0';
	for (d = 0, n = 0; buf[n] != '\0'; n++)
		if (buf[n] == '.')
			d++;
	if (d > 1) {
		src = p;
		syntax();
	}
	src = p;
	return chk(strtod(buf, NULL));
}

static double expr(void);

static double
callfn(const struct func *f)
{
	double a[MAXARGS];
	int n = 0, c;

	if (peek() == ')') {
		src++;
	} else {
		for (;;) {
			if (n == MAXARGS)
				fail("too many arguments");
			a[n++] = expr();
			c = peek();
			src++;
			if (c == ')')
				break;
			if (c != ',') {
				src--;
				syntax();
			}
		}
	}
	if (n < f->min || n > f->max)
		fail("wrong number of arguments");
	return apply(f->id, a, n);
}

static double
primary(void)
{
	char name[VNAME + 1];
	const struct func *f;
	double v;
	int c = peek();

	if (c == '(') {
		src++;
		v = expr();
		if (peek() != ')')
			syntax();
		src++;
		return v;
	}
	if (isdigit(c) || c == '.')
		return number();
	if (isalpha(c) || c == '_') {
		ident(name);
		if (peek() == '(') {
			f = findfunc(name);
			if (f == NULL)
				failname("unknown function", name);
			src++;
			return callfn(f);
		}
		if (!lookup(name, &v)) {
			if (findfunc(name) != NULL)
				failname("function needs arguments", name);
			failname("unknown name", name);
		}
		return v;
	}
	syntax();
}

static double
postfix(void)
{
	double v = primary();

	while (peek() == '!') {
		src++;
		v = factorial(v);
	}
	return v;
}

static double unary(void);

static double
power(void)
{
	double b = postfix();

	if (peek() == '^') {
		src++;
		b = dpow(b, unary());
	}
	return b;
}

static double
unary(void)
{
	double v;
	int c;

	if (++depth > MAXDEPTH + 1)
		fail("nested too deeply");
	c = peek();
	if (c == '-') {
		src++;
		v = -unary();
	} else if (c == '+') {
		src++;
		v = unary();
	} else {
		v = power();
	}
	depth--;
	return v;
}

static double
term(void)
{
	double v = unary();
	int c;

	for (;;) {
		c = peek();
		if (c != '*' && c != '/' && c != '%')
			return v;
		src++;
		v = arith(c, v, unary());
	}
}

static double
expr(void)
{
	double v = term();
	int c;

	for (;;) {
		c = peek();
		if (c != '+' && c != '-')
			return v;
		src++;
		v = arith(c, v, term());
	}
}

static double
evalstr(const char *s)
{
	double v;

	line0 = src = s;
	depth = 0;
	v = expr();
	if (peek() != '\0')
		syntax();
	return v;
}

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */

/*
 * Output goes through write(2) from one buffer, so the signal handler of the
 * terminal interface and the normal path share a single descriptor and no
 * stdio buffer is allocated.
 */
static char obuf[512];
static size_t olen;

/*
 * write(2) for output whose failure leaves nothing to do: the terminal is
 * gone or an error is already on its way out. The result is taken into a
 * variable because glibc marks write warn_unused_result under
 * _FORTIFY_SOURCE, and a cast to void does not silence that.
 */
static void
put_fd(int fd, const void *p, size_t n)
{
	ssize_t r = write(fd, p, n);

	(void)r;
}

static void
oflush(void)
{
	size_t done = 0;
	long w;

	while (done < olen) {
		w = (long)write(1, obuf + done, olen - done);
		if (w <= 0)
			break;
		done += (size_t)w;
	}
	olen = 0;
}

static void
owrite(const char *p, size_t n)
{
	size_t k;

	while (n > 0) {
		if (olen == sizeof obuf)
			oflush();
		k = sizeof obuf - olen;
		if (k > n)
			k = n;
		memcpy(obuf + olen, p, k);
		olen += k;
		p += k;
		n -= k;
	}
}

static void
oputs(const char *s)
{
	owrite(s, strlen(s));
}

static void
oputc(int c)
{
	char ch = (char)c;

	owrite(&ch, 1);
}

static void
oline(const char *s)
{
	oputs(s);
	oputc('\n');
}

static void
outf(const char *fmt, ...)
{
	char b[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	oputs(b);
}

/*
 * The text of x with at most `digits` significant digits, in the style of
 * printf("%.*g"): fixed notation for decimal exponents from -4 to digits - 1,
 * exponential otherwise, trailing zeros removed. The digits come from "%e".
 * The target's "%g" (lib/libc/stdio/doprnt_float.c) counts the zeros after
 * the decimal point of a value below 0.1 as significant digits and so prints
 * too few of them; its "%e" does not.
 */
static void
fmtnum(char *buf, size_t n, double x)
{
	char raw[40], d[24], out[40];
	const char *p = raw;
	char *o = out;
	int ex, nd = 0, i;

	if (x == 0.0) {
		snprintf(buf, n, "0");
		return;
	}
	snprintf(raw, sizeof raw, "%.*e", digits - 1, x);
	if (*p == '-') {
		*o++ = '-';
		p++;
	}
	for (; *p != 'e' && *p != '\0'; p++)
		if (*p != '.' && nd < (int)sizeof d)
			d[nd++] = *p;
	ex = atoi(p + 1);
	while (nd > 1 && d[nd - 1] == '0')
		nd--;
	if (ex < -4 || ex >= digits) {
		*o++ = d[0];
		if (nd > 1) {
			*o++ = '.';
			for (i = 1; i < nd; i++)
				*o++ = d[i];
		}
		*o++ = 'e';
		*o++ = ex < 0 ? '-' : '+';
		i = ex < 0 ? -ex : ex;
		if (i >= 100)
			*o++ = (char)('0' + i / 100);
		*o++ = (char)('0' + i / 10 % 10);
		*o++ = (char)('0' + i % 10);
	} else if (ex >= 0) {
		for (i = 0; i <= ex; i++)
			*o++ = i < nd ? d[i] : '0';
		if (nd > ex + 1) {
			*o++ = '.';
			for (i = ex + 1; i < nd; i++)
				*o++ = d[i];
		}
	} else {
		*o++ = '0';
		*o++ = '.';
		for (i = 0; i < -ex - 1; i++)
			*o++ = '0';
		for (i = 0; i < nd; i++)
			*o++ = d[i];
	}
	*o = '\0';
	snprintf(buf, n, "%s", out);
}

static void
printnum(double x)
{
	char buf[40];

	fmtnum(buf, sizeof buf, x);
	oline(buf);
}

static void
printkv(const char *key, double x)
{
	char buf[40];

	fmtnum(buf, sizeof buf, x);
	outf("%s %s\n", key, buf);
}

/* ------------------------------------------------------------------ */
/* Programmer mode                                                    */
/* ------------------------------------------------------------------ */

static word
wmask(void)
{
	return bits >= 32 ? 0xffffffffu : (((word)1 << bits) - 1u);
}

static word
tword(double v)
{
	if (v < 0.0 || v >= 4294967296.0 || v != floor(v))
		fail("not an integer word");
	return (word)v & wmask();
}

static word pexpr(void);

static word
pnumber(void)
{
	word v = 0;
	int base = 10, d;

	if (src[0] == '0' && (src[1] == 'x' || src[1] == 'X'))
		base = 16;
	else if (src[0] == '0' && (src[1] == 'b' || src[1] == 'B'))
		base = 2;
	else if (src[0] == '0' && (src[1] == 'o' || src[1] == 'O'))
		base = 8;
	if (base != 10)
		src += 2;
	if (digitval((unsigned char)*src) >= base)
		syntax();
	while ((d = digitval((unsigned char)*src)) < base) {
		if (v > (0xffffffffu - (word)d) / (word)base)
			fail("value too large");
		v = v * (word)base + (word)d;
		src++;
	}
	if (v > wmask())
		fail("value too large for word size");
	return v;
}

static word
protate(word x, word n, int left)
{
	n %= (word)bits;
	if (n == 0)
		return x;
	if (!left)
		n = (word)bits - n;
	return ((x << n) | (x >> ((word)bits - n))) & wmask();
}

static word
pprimary(void)
{
	char name[VNAME + 1];
	word v, a, b;
	double dv;
	int c = peek();

	if (c == '(') {
		src++;
		v = pexpr();
		if (peek() != ')')
			syntax();
		src++;
		return v;
	}
	if (isdigit(c))
		return pnumber();
	if (isalpha(c) || c == '_') {
		ident(name);
		if (peek() == '(') {
			if (strcmp(name, "rol") != 0 && strcmp(name, "ror") != 0)
				failname("unknown function", name);
			src++;
			a = pexpr();
			if (peek() != ',')
				syntax();
			src++;
			b = pexpr();
			if (peek() != ')')
				syntax();
			src++;
			return protate(a, b, name[2] == 'l');
		}
		if (!lookup(name, &dv))
			failname("unknown name", name);
		return tword(dv);
	}
	syntax();
}

static word
punary(void)
{
	word v;
	int c;

	if (++depth > MAXDEPTH + 1)
		fail("nested too deeply");
	c = peek();
	if (c == '~') {
		src++;
		v = ~punary() & wmask();
	} else if (c == '-') {
		src++;
		v = (0u - punary()) & wmask();
	} else if (c == '+') {
		src++;
		v = punary();
	} else {
		v = pprimary();
	}
	depth--;
	return v;
}

static word
pmul(void)
{
	word v = punary(), r;
	int c;

	for (;;) {
		c = peek();
		if (c != '*' && c != '/' && c != '%')
			return v;
		src++;
		r = punary();
		if (c == '*') {
			v = (v * r) & wmask();
		} else {
			if (r == 0)
				fail("division by zero");
			v = c == '/' ? v / r : v % r;
		}
	}
}

static word
padd(void)
{
	word v = pmul(), r;
	int c;

	for (;;) {
		c = peek();
		if (c != '+' && c != '-')
			return v;
		src++;
		r = pmul();
		v = (c == '+' ? v + r : v - r) & wmask();
	}
}

static word
pshift(void)
{
	word v = padd(), r;

	for (;;) {
		peek();
		if (src[0] == '<' && src[1] == '<') {
			src += 2;
			r = padd();
			v = r >= (word)bits ? 0u : (v << r) & wmask();
		} else if (src[0] == '>' && src[1] == '>') {
			src += 2;
			r = padd();
			v = r >= (word)bits ? 0u : v >> r;
		} else {
			return v;
		}
	}
}

static word
pand(void)
{
	word v = pshift();

	while (peek() == '&') {
		src++;
		v &= pshift();
	}
	return v;
}

static word
pxor(void)
{
	word v = pand();

	while (peek() == '^') {
		src++;
		v ^= pand();
	}
	return v;
}

static word
pexpr(void)
{
	word v = pxor();

	while (peek() == '|') {
		src++;
		v |= pxor();
	}
	return v;
}

static word
pevalstr(const char *s)
{
	word v;

	line0 = src = s;
	depth = 0;
	v = pexpr();
	if (peek() != '\0')
		syntax();
	return v;
}

static void
printword(word w)
{
	char b[48];
	int i, n = 0;

	outf("hex  0x%lX\n", (unsigned long)w);
	outf("dec  %lu", (unsigned long)w);
	if ((w >> (bits - 1)) & 1u)
		outf(" (%ld)", (long)(int32_t)(w | ~wmask()));
	oputc('\n');
	outf("oct  0o%lo\n", (unsigned long)w);
	for (i = bits - 1; i >= 0; i--) {
		b[n++] = (char)('0' + ((w >> i) & 1u));
		if (i > 0 && i % 4 == 0)
			b[n++] = ' ';
	}
	b[n] = '\0';
	outf("bin  %s\n", b);
}

/* ------------------------------------------------------------------ */
/* RPN                                                                */
/* ------------------------------------------------------------------ */

static void
need(int n)
{
	if (sp < n)
		fail("stack empty");
}

static void
push(double v)
{
	if (sp == STACKMAX)
		fail("stack full");
	stack[sp++] = v;
}

static char *
nexttok(char **sp_, char *tok, size_t size)
{
	char *s = *sp_;
	size_t n = 0;

	while (isspace((unsigned char)*s))
		s++;
	while (*s != '\0' && !isspace((unsigned char)*s)) {
		if (n == size - 1)
			fail("token too long");
		tok[n++] = *s++;
	}
	tok[n] = '\0';
	*sp_ = s;
	return n != 0 ? tok : NULL;
}

static int
isnumtok(const char *t)
{
	if (t[0] == '-' || t[0] == '+')
		t++;
	return isdigit((unsigned char)t[0]) || t[0] == '.';
}

static void
rpn_token(const char *tok, char **rest)
{
	char arg[40];
	const struct func *f;
	double v, t;
	int neg = 0, k;

	if (isnumtok(tok)) {
		if (tok[0] == '-' || tok[0] == '+')
			neg = *tok++ == '-';
		line0 = src = tok;
		v = number();
		if (*src != '\0')
			failname("bad token", tok);
		push(neg ? -v : v);
		return;
	}
	if (tok[1] == '\0' && strchr("+-*/%^", tok[0]) != NULL) {
		need(2);
		v = arith(tok[0], stack[sp - 2], stack[sp - 1]);
		sp--;
		stack[sp - 1] = v;
		return;
	}
	if (strcmp(tok, "neg") == 0) {
		need(1);
		stack[sp - 1] = -stack[sp - 1];
	} else if (strcmp(tok, "!") == 0) {
		need(1);
		stack[sp - 1] = factorial(stack[sp - 1]);
	} else if (strcmp(tok, "dup") == 0) {
		need(1);
		push(stack[sp - 1]);
	} else if (strcmp(tok, "drop") == 0) {
		need(1);
		sp--;
	} else if (strcmp(tok, "swap") == 0) {
		need(2);
		t = stack[sp - 1];
		stack[sp - 1] = stack[sp - 2];
		stack[sp - 2] = t;
	} else if (strcmp(tok, "over") == 0) {
		need(2);
		push(stack[sp - 2]);
	} else if (strcmp(tok, "roll") == 0) {
		need(1);
		t = stack[sp - 1];
		memmove(&stack[1], &stack[0], (size_t)(sp - 1) * sizeof stack[0]);
		stack[0] = t;
	} else if (strcmp(tok, "rollu") == 0) {
		need(1);
		t = stack[0];
		memmove(&stack[0], &stack[1], (size_t)(sp - 1) * sizeof stack[0]);
		stack[sp - 1] = t;
	} else if (strcmp(tok, "sto") == 0 || strcmp(tok, "rcl") == 0) {
		if (nexttok(rest, arg, sizeof arg) == NULL)
			fail("sto and rcl need a name");
		if (tok[0] == 's') {
			need(1);
			setvar(arg, stack[sp - 1]);
		} else {
			if (!lookup(arg, &v))
				failname("unknown name", arg);
			push(v);
		}
	} else if ((f = findfunc(tok)) != NULL) {
		k = f->min == f->max ? f->min : sp;
		need(k);
		v = apply(f->id, &stack[sp - k], k);
		sp -= k;
		push(v);
	} else if (lookup(tok, &v)) {
		push(v);
	} else {
		failname("unknown word", tok);
	}
}

static void
rpn_line(char *s)
{
	char tok[40];

	while (nexttok(&s, tok, sizeof tok) != NULL)
		rpn_token(tok, &s);
	if (sp == 0) {
		oline("empty");
	} else {
		ans = stack[sp - 1];
		printnum(ans);
	}
}

static void
cmd_stack(void)
{
	int i;

	if (sp == 0)
		oline("empty");
	for (i = sp - 1; i >= 0; i--) {
		char buf[40];

		fmtnum(buf, sizeof buf, stack[i]);
		outf("%d: %s\n", sp - i, buf);
	}
}

/* ------------------------------------------------------------------ */
/* Lists, statistics and unit conversion                              */
/* ------------------------------------------------------------------ */

/* Evaluate each comma or blank separated item of s into a. */
static int
getlist(char *s, double *a)
{
	char tok[LINEMAX];
	size_t n;
	int cnt = 0;

	for (;;) {
		while (isspace((unsigned char)*s) || *s == ',')
			s++;
		if (*s == '\0')
			return cnt;
		n = 0;
		while (*s != '\0' && !isspace((unsigned char)*s) && *s != ',') {
			if (n == sizeof tok - 1)
				fail("token too long");
			tok[n++] = *s++;
		}
		tok[n] = '\0';
		if (cnt == STATMAX)
			fail("too many values");
		a[cnt++] = evalstr(tok);
	}
}

static void
cmd_stat(char *rest)
{
	static double a[STATMAX], scratch[STATMAX];
	double r;
	int n;

	n = getlist(rest, a);
	if (n == 0) {
		oline("usage: stat v1 v2 ...  (blank or comma separated)");
		oline("       reg x1 x2 ... ; y1 y2 ...");
		return;
	}
	outf("n %d\n", n);
	cstatus(calc_sum(a, (size_t)n, &r));
	printkv("sum", r);
	cstatus(calc_mean(a, (size_t)n, &r));
	printkv("mean", r);
	ans = r;
	cstatus(calc_median(a, (size_t)n, scratch, &r));
	printkv("median", r);
	cstatus(calc_min(a, (size_t)n, &r, NULL));
	printkv("min", r);
	cstatus(calc_max(a, (size_t)n, &r, NULL));
	printkv("max", r);
	if (n >= 2) {
		cstatus(calc_variance(a, (size_t)n, 1, &r));
		printkv("var", r);
		cstatus(calc_stddev(a, (size_t)n, 1, &r));
		printkv("stdev", r);
	}
	cstatus(calc_variance(a, (size_t)n, 0, &r));
	printkv("varp", r);
	cstatus(calc_stddev(a, (size_t)n, 0, &r));
	printkv("stdevp", r);
}

static void
cmd_reg(char *rest)
{
	static double x[STATMAX], y[STATMAX];
	double mx, my, sxx = 0.0, sxy = 0.0, r;
	char *semi = strchr(rest, ';');
	int n, m, i;

	if (semi == NULL)
		fail("usage: reg x1 x2 ... ; y1 y2 ...");
	*semi = '\0';
	n = getlist(rest, x);
	m = getlist(semi + 1, y);
	if (n != m)
		fail("lists differ in length");
	if (n < 2)
		fail("not enough values");
	cstatus(calc_mean(x, (size_t)n, &mx));
	cstatus(calc_mean(y, (size_t)n, &my));
	for (i = 0; i < n; i++) {
		sxx += (x[i] - mx) * (x[i] - mx);
		sxy += (x[i] - mx) * (y[i] - my);
	}
	if (sxx == 0.0)
		fail("domain error");
	cstatus(calc_correl(x, y, (size_t)n, &r));
	outf("n %d\n", n);
	printkv("slope", chk(sxy / sxx));
	printkv("intercept", chk(my - sxy / sxx * mx));
	printkv("r", r);
	ans = r;
}

/* Strip the last blank separated word of s (ending at *endp). */
static char *
lastword(char *s, char **endp)
{
	char *e = *endp, *w;

	while (e > s && isspace((unsigned char)e[-1]))
		e--;
	w = e;
	while (w > s && !isspace((unsigned char)w[-1]))
		w--;
	*e = '\0';
	*endp = w;
	return w;
}

static void
cmd_conv(char *rest)
{
	const struct unit *uf, *ut;
	char *end = rest + strlen(rest), *to, *from;
	double v, base;

	to = lastword(rest, &end);
	from = lastword(rest, &end);
	if (*to == '\0' || *from == '\0' || *rest == '\0' || from == rest)
		fail("usage: conv VALUE FROM TO");
	while (end > rest && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	if (*rest == '\0')
		fail("usage: conv VALUE FROM TO");
	uf = findunit(from);
	if (uf == NULL)
		failname("unknown unit", from);
	ut = findunit(to);
	if (ut == NULL)
		failname("unknown unit", to);
	if (uf->dim != ut->dim)
		fail("incompatible units");
	v = evalstr(rest);
	base = v * uf->f + uf->off;
	v = chk((base - ut->off) / ut->f);
	ans = v;
	printnum(v);
}

static void
cmd_units(void)
{
	int i, col = 0, last = -1;

	for (i = 0; i < NUNIT; i++) {
		size_t len = strlen(units[i].name);

		if (units[i].dim != last) {
			if (col != 0)
				oputc('\n');
			outf("%s:", dimnames[units[i].dim]);
			col = (int)strlen(dimnames[units[i].dim]) + 1;
			last = units[i].dim;
		}
		if (col + (int)len + 1 > SCREEN_COLS) {
			outf("\n ");
			col = 1;
		}
		outf(" %s", units[i].name);
		col += (int)len + 1;
	}
	oputc('\n');
}

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */

static void
cmd_help(void)
{
	oline("calc: scientific calculator");
	oline(" 2+3*4  sin(pi/6)  2^-3  5!  x=7  ans  m0..m9");
	oline(" modes: alg  rpn  prog    angle: deg  rad");
	oline(" rpn: 3 4 + swap dup drop over roll sto rcl");
	oline(" prog: & | ^ ~ << >> rol ror 0x 0b 0o  bits N");
	oline(" conv 100 degC degF    units");
	oline(" stat v1 v2 ..    reg x1 x2 .. ; y1 y2 ..");
	oline(" const  vars  stack  digits N  clr  quit");
	oline(" F1 help F2 alg F3 rpn F4 prog F5 deg/rad");
	oline(" F6 units F7 stat F8 const F9 clr F10 quit");
}

static void
cmd_const(void)
{
	char buf[40];
	int i;

	for (i = 0; i < NKONST; i++) {
		fmtnum(buf, sizeof buf, konsts[i].v);
		outf("%s %s\n", konsts[i].name, buf);
	}
}

static void
cmd_vars(void)
{
	char buf[40];
	int i;

	for (i = 0; i < NMEM; i++)
		if (mem[i] != 0.0) {
			fmtnum(buf, sizeof buf, mem[i]);
			outf("m%d %s\n", i, buf);
		}
	for (i = 0; i < nvars; i++) {
		fmtnum(buf, sizeof buf, vars[i].v);
		outf("%s %s\n", vars[i].name, buf);
	}
}

static void
cmd_clr(void)
{
	if (mode == M_RPN) {
		sp = 0;
		oline("stack cleared");
	} else {
		nvars = 0;
		ans = 0.0;
		memset(mem, 0, sizeof mem);
		oline("cleared");
	}
}

static void
switch_mode(int m, const char *name)
{
	mode = m;
	outf("mode %s\n", name);
}

static int
argint(const char *s, int lo, int hi)
{
	double v;

	if (*s == '\0')
		fail("missing number");
	v = evalstr(s);
	if (v != floor(v) || v < (double)lo || v > (double)hi)
		fail("value out of range");
	return (int)v;
}

/* Run a command line when its first word is one; 1 when handled. */
static int
command(char *s)
{
	char *rest;
	size_t n = strcspn(s, " \t");
	int i;

	for (i = 0; i < C_COUNT; i++)
		if (strlen(cmdwords[i]) == n && strncmp(cmdwords[i], s, n) == 0)
			break;
	if (i == C_COUNT)
		return 0;
	rest = s + n;
	while (isspace((unsigned char)*rest))
		rest++;
	switch (i) {
	case C_HELP:
	case C_HELP2:
		cmd_help();
		break;
	case C_QUIT:
	case C_EXIT:
		quitting = 1;
		break;
	case C_DEG:
		degmode = 1;
		oline("angle degrees");
		break;
	case C_RAD:
		degmode = 0;
		oline("angle radians");
		break;
	case C_ANGLE:
		degmode = !degmode;
		oline(degmode ? "angle degrees" : "angle radians");
		break;
	case C_ALG:
		switch_mode(M_ALG, "alg");
		break;
	case C_RPN:
		switch_mode(M_RPN, "rpn");
		break;
	case C_PROG:
		switch_mode(M_PROG, "prog");
		break;
	case C_BITS:
		n = (size_t)argint(rest, 8, 32);
		if (n != 8 && n != 16 && n != 32)
			fail("bits must be 8, 16 or 32");
		bits = (int)n;
		outf("bits %d\n", bits);
		break;
	case C_DIGITS:
		digits = argint(rest, 1, 17);
		outf("digits %d\n", digits);
		break;
	case C_CLR:
	case C_CLEAR:
		cmd_clr();
		break;
	case C_VARS:
		cmd_vars();
		break;
	case C_CONST:
		cmd_const();
		break;
	case C_UNITS:
		cmd_units();
		break;
	case C_CONV:
		cmd_conv(rest);
		break;
	case C_STAT:
		cmd_stat(rest);
		break;
	case C_REG:
		cmd_reg(rest);
		break;
	default:
		cmd_stack();
		break;
	}
	return 1;
}

/* name = expression at the start of s; 1 and the rest when it matches. */
static int
assignment(const char *s, char *name, const char **rest)
{
	line0 = src = s;
	if (!isalpha(peek()) && *src != '_')
		return 0;
	ident(name);
	if (peek() != '=' || src[1] == '=')
		return 0;
	*rest = src + 1;
	return 1;
}

static void
dispatch(char *s)
{
	char name[VNAME + 1];
	const char *rest;
	double v;

	if (command(s))
		return;
	switch (mode) {
	case M_RPN:
		rpn_line(s);
		break;
	case M_PROG:
		if (assignment(s, name, &rest)) {
			v = (double)pevalstr(rest);
			setvar(name, v);
		} else {
			v = (double)pevalstr(s);
		}
		ans = v;
		printword((word)v);
		break;
	default:
		if (assignment(s, name, &rest)) {
			v = evalstr(rest);
			setvar(name, v);
		} else {
			v = evalstr(s);
		}
		ans = v;
		printnum(v);
		break;
	}
}

static char *curline;

static void
run_line(char *s)
{
	char *e;

	while (isspace((unsigned char)*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = '\0';
	if (*s == '\0')
		return;
	curline = s;
	if (setjmp(errjmp) == 0) {
		dispatch(curline);
	} else {
		outf("error: %s\n", errmsg);
		had_error = 1;
	}
}

/* ------------------------------------------------------------------ */
/* Terminal interface                                                 */
/* ------------------------------------------------------------------ */

#ifndef CALC_NO_TTY

enum {
	K_NONE = 0, K_EOF = -1, K_ESC = 0x100, K_UP, K_DOWN,
	K_F1 = 0x200	/* K_F1 + n is F(n + 1) */
};

#define HIST		4
#define BAR_ITEMS	10

static const char *const fkeys[BAR_ITEMS] = {
	"help", "alg", "rpn", "prog", "angle", "units", "stat", "const",
	"clr", "quit"
};

/*
 * tty_raw is set while the terminal is in cbreak mode, so a handler knows
 * whether there is a mode to restore; tty_resumed asks the input loop to
 * redraw after a stop and continue.
 */
static volatile sig_atomic_t tty_raw, tty_resumed;
static int rows = SCREEN_ROWS, cols = SCREEN_COLS;
static char reset_seq[48];
static int reset_len;
static char hist[HIST][LINEMAX];
static int nhist;

#ifdef HOSTBUILD
static struct termios tty_saved;

static void
tty_save(void)
{
	tcgetattr(0, &tty_saved);
}

static void
tty_cbreak(void)
{
	struct termios t;

	t = tty_saved;
	t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);
}

static void
tty_restore(void)
{
	tcsetattr(0, TCSANOW, &tty_saved);
}
#else
static struct sgttyb tty_saved;

static void
tty_save(void)
{
	ioctl(0, TIOCGETP, &tty_saved);
}

static void
tty_cbreak(void)
{
	struct sgttyb sg;

	sg = tty_saved;
	sg.sg_flags |= CBREAK;
	sg.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &sg);
}

static void
tty_restore(void)
{
	ioctl(0, TIOCSETP, &tty_saved);
}
#endif

/* Undo everything tty_start did; safe to call from a signal handler. */
static void
tty_leave(void)
{
	if (!tty_raw)
		return;
	tty_raw = 0;
	tty_restore();
	put_fd(1, reset_seq, (size_t)reset_len);
}

/*
 * The flag goes up before the mode changes, so a signal that lands
 * between the two restores the saved mode, which is then still current.
 */
static void
tty_enter(void)
{
	tty_raw = 1;
	tty_cbreak();
}

static void
on_signal(int sig)
{
	tty_leave();
	_exit(128 + sig);
}

/*
 * A stop signal (^Z in cbreak mode) restores the terminal before the
 * process stops, so the shell gets its modes back, then re-enters cbreak
 * mode on continue. The redraw waits for the input loop, because the
 * screen code is not safe to run inside a handler.
 */
static void
on_stop(int sig)
{
	sigset_t set;

	tty_leave();
	signal(sig, SIG_DFL);
	(void)sigemptyset(&set);
	(void)sigaddset(&set, sig);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	kill(getpid(), sig);
	signal(sig, on_stop);
	tty_enter();
	tty_resumed = 1;
}

static void
on_exit_restore(void)
{
	tty_leave();
}

static int
rdbyte(void)
{
	unsigned char c;

	if (read(0, &c, 1) != 1)
		return K_EOF;
	return c;
}

static int
wait_input(int ms)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000L;
	return select(1, &fds, 0, 0, &tv);
}

static int
tilde_key(int n)
{
	static const unsigned char codes[BAR_ITEMS] = {
		11, 12, 13, 14, 15, 17, 18, 19, 20, 21
	};
	int i;

	for (i = 0; i < BAR_ITEMS; i++)
		if (codes[i] == n)
			return K_F1 + i;
	return K_NONE;
}

/*
 * One key. F1 to F10 arrive as ESC O P..S and ESC [ 15 ~ .. 21 ~ from the
 * PicoCalc keyboard and from an xterm; ESC [ 11 ~ .. 14 ~ (vt220), ESC [ [ A
 * .. E (Linux console) and ESC [ 1 ; m P..S (modified xterm F1 to F4) are
 * accepted as well. A lone ESC returns K_ESC after 50 ms.
 */
static int
getkey(void)
{
	int c, n, num[2] = { 0, 0 }, np = 0;

	c = rdbyte();
	if (c != 033)
		return c;
	if (wait_input(50) <= 0)
		return K_ESC;
	c = rdbyte();
	if (c == 'O') {
		if (wait_input(100) <= 0)
			return K_NONE;
		c = rdbyte();
		return c >= 'P' && c <= 'S' ? K_F1 + (c - 'P') : K_NONE;
	}
	if (c != '[')
		return K_NONE;
	for (n = 0; n < 10; n++) {
		if (wait_input(100) <= 0)
			return K_NONE;
		c = rdbyte();
		if (c < 0)
			return K_EOF;
		if (c >= '0' && c <= '9') {
			num[np] = num[np] * 10 + (c - '0');
			continue;
		}
		if (c == ';') {
			if (np < 1)
				np++;
			continue;
		}
		if (c == '[' && n == 0) {
			if (wait_input(100) <= 0)
				return K_NONE;
			c = rdbyte();
			return c >= 'A' && c <= 'E' ? K_F1 + (c - 'A') : K_NONE;
		}
		switch (c) {
		case '~':
			return tilde_key(num[0]);
		case 'A':
			return K_UP;
		case 'B':
			return K_DOWN;
		case 'P':
		case 'Q':
		case 'R':
		case 'S':
			return num[0] == 1 ? K_F1 + (c - 'P') : K_NONE;
		default:
			if (c >= 0x40 && c <= 0x7e)
				return K_NONE;
		}
	}
	return K_NONE;
}

static const char *
prompt(void)
{
	return mode == M_RPN ? "rpn> " : (mode == M_PROG ? "prog> " : "> ");
}

/* Repaint the input row: prompt and the tail of the line that fits. */
static void
show_input(const char *line, int len)
{
	int room = cols - (int)strlen(prompt()) - 1;
	int skip = len > room ? len - room : 0;

	oputs("\r\033[K");
	oputs(prompt());
	owrite(line + skip, (size_t)(len - skip));
}

/*
 * The item bar on the last row. Each item is its F-key number in normal
 * video followed by a four column label in reverse video; the item of the
 * current mode is drawn in normal video. Items one to nine take five
 * columns and item ten six, so the bar is 51 columns and pads to 53.
 */
static void
bar_draw(void)
{
	static const char *const labels[BAR_ITEMS] = {
		"Help", "Alg", "RPN", "Prog", "Deg", "Unit", "Stat", "Cnst",
		"Clr", "Quit"
	};
	const char *label;
	int i, active;

	outf("\0337\033[%d;1H", rows);
	for (i = 0; i < BAR_ITEMS; i++) {
		label = labels[i];
		if (i == 4 && !degmode)
			label = "Rad";
		active = (i == 1 && mode == M_ALG) || (i == 2 && mode == M_RPN) ||
		    (i == 3 && mode == M_PROG);
		outf("\033[0m%d\033[%sm%-4s", i + 1, active ? "1" : "7", label);
	}
	outf("\033[7m%*s\033[0m\0338", SCREEN_COLS - 51, "");
}

static void
screen_init(void)
{
	outf("\033[0m\033[2J\033[1;%dr\033[1;1H", rows - 1);
	oline("calc: F1 help  F10 quit");
	bar_draw();
}

static void
tty_start(void)
{
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
		if (ws.ws_row >= 8)
			rows = ws.ws_row;
		if (ws.ws_col >= 20)
			cols = ws.ws_col;
	}
	reset_len = snprintf(reset_seq, sizeof reset_seq,
	    "\033[r\033[0m\033[%d;1H\033[2K", rows);
	tty_save();
	atexit(on_exit_restore);
	signal(SIGINT, on_signal);
	signal(SIGQUIT, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGTSTP, on_stop);
	tty_enter();
	screen_init();
}

static void
remember(const char *line)
{
	if (line[0] == '\0' || (nhist > 0 && strcmp(hist[nhist - 1], line) == 0))
		return;
	if (nhist == HIST) {
		memmove(hist[0], hist[1], sizeof hist[0] * (HIST - 1));
		nhist--;
	}
	strcpy(hist[nhist++], line);
}

static void
run_fkey(int n)
{
	char cmd[16];

	strcpy(cmd, fkeys[n]);
	oputs("\r\033[K");
	run_line(cmd);
	bar_draw();
}

static int
interact(void)
{
	char line[LINEMAX];
	int len, k, h;

	tty_start();
	while (!quitting) {
		len = 0;
		line[0] = '\0';
		h = nhist;
		show_input(line, len);
		for (;;) {
			oflush();
			k = getkey();
			if (tty_resumed) {
				tty_resumed = 0;
				screen_init();
				show_input(line, len);
			}
			if (k == K_EOF) {
				quitting = 1;
				break;
			}
			if (k >= K_F1 && k < K_F1 + BAR_ITEMS) {
				run_fkey(k - K_F1);
				if (quitting)
					break;
				show_input(line, len);
				continue;
			}
			if (k == '\r' || k == '\n') {
				line[len] = '\0';
				remember(line);
				oputc('\n');
				run_line(line);
				bar_draw();
				break;
			}
			if (k == 0x7f || k == '\b') {
				if (len > 0)
					len--;
				show_input(line, len);
			} else if (k == 0x15 || k == K_ESC) {
				len = 0;
				show_input(line, len);
			} else if (k == 0x0c) {
				screen_init();
				show_input(line, len);
			} else if (k == 0x04) {
				if (len == 0) {
					quitting = 1;
					break;
				}
			} else if (k == K_UP || k == K_DOWN) {
				if (k == K_UP && h > 0)
					h--;
				else if (k == K_DOWN && h < nhist)
					h++;
				if (h < nhist)
					strcpy(line, hist[h]);
				else
					line[0] = '\0';
				len = (int)strlen(line);
				show_input(line, len);
			} else if (k >= 0x20 && k < 0x7f && len < LINEMAX - 1) {
				line[len++] = (char)k;
				if (len + (int)strlen(prompt()) < cols - 1)
					oputc(k);
				else
					show_input(line, len);
			}
		}
	}
	oflush();
	return 0;
}

#endif /* !CALC_NO_TTY */

/* ------------------------------------------------------------------ */
/* Line interface                                                     */
/* ------------------------------------------------------------------ */

static int
usage(void)
{
	static const char msg[] = "usage: calc [-dlpr] [expression ...]\n";

	put_fd(2, msg, sizeof msg - 1);
	return 2;
}

static char ibuf[256];
static size_t ipos, ilen;

/* One input byte from fd 0, or -1 at end of input. */
static int
getbyte(void)
{
	long n;

	if (ipos == ilen) {
		n = (long)read(0, ibuf, sizeof ibuf);
		if (n <= 0)
			return -1;
		ilen = (size_t)n;
		ipos = 0;
	}
	return (unsigned char)ibuf[ipos++];
}

static void
line_loop(void)
{
	char line[LINEMAX];
	size_t n;
	int c, toolong;

	while (!quitting) {
		n = 0;
		toolong = 0;
		while ((c = getbyte()) >= 0 && c != '\n') {
			if (n < sizeof line - 1)
				line[n++] = (char)c;
			else
				toolong = 1;
		}
		if (c < 0 && n == 0 && !toolong)
			break;
		line[n] = '\0';
		if (toolong) {
			oline("error: line too long");
			had_error = 1;
		} else {
			run_line(line);
		}
		oflush();
		if (c < 0)
			break;
	}
}

int
main(int argc, char **argv)
{
	char line[LINEMAX];
	const char *o;
	int i, forced_line = 0;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
		if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		}
		for (o = argv[i] + 1; *o != '\0'; o++) {
			if (*o == 'r')
				mode = M_RPN;
			else if (*o == 'p')
				mode = M_PROG;
			else if (*o == 'd')
				degmode = 1;
			else if (*o == 'l')
				forced_line = 1;
			else
				return usage();
		}
	}
	if (i < argc) {
		for (; i < argc && !quitting; i++) {
			if (strlen(argv[i]) >= sizeof line) {
				oline("error: line too long");
				had_error = 1;
				continue;
			}
			strcpy(line, argv[i]);
			run_line(line);
		}
		oflush();
		return had_error ? 1 : 0;
	}
#ifndef CALC_NO_TTY
	if (!forced_line && isatty(0) && isatty(1))
		return interact();
#else
	(void)forced_line;
#endif
	line_loop();
	oflush();
	return had_error ? 1 : 0;
}
