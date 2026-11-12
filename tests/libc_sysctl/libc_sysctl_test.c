/*
 * Host gate for the libc callers that read a value through sysctl(3) into a
 * fixed buffer, compiled from lib/libc/gen and linked against a sysctl()
 * this file supplies.
 *
 * sysctl(3) reports the length the value needs rather than the length it
 * copied, so a buffer shorter than the value comes back holding a prefix and
 * a length larger than itself. A caller that takes that length for the amount
 * it received walks past the end of its own buffer, and a caller that treats
 * the prefix as a string reads past it. Both are properties of the caller
 * rather than of the kernel, so this gate models the contract instead of
 * linking sys/kern: tests/kernel/sysctl_test.c holds the kernel to it, and
 * this one holds libc to the same statement.
 *
 * test_sysctl() answers the way sys/kern/kern_sysctl.c's helpers do: it
 * copies MIN(value, *oldlenp), assigns the value's whole length to *oldlenp,
 * and reports ENOMEM when the two differ.
 *
 * knlist() spells a kernel symbol name into the components after
 * {CTL_MACHDEP, CPU_NLIST} as sys/sysctl.h defines at CTL_NLIST_BYTES and
 * passes no new value. test_sysctl() answers that node only for the exact
 * components written out below as literals, so an encoder that orders the
 * bytes differently, drops the terminator or pads with another component
 * finds no symbol.
 *
 * This file carries the tree's headers alone; libc_shim.h says why.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <machine/cpu.h>
#include <nlist.h>
#include <string.h>
#include <errno.h>

#include "libc_shim.h"

int	uname(struct utsname *);
int	gethostname(char *, int);
int	knlist(struct nlist *);

/*
 * The error number include/errno.h declares, which the Makefile renames so
 * that it does not collide with the host libc's thread-local one. On the
 * target it is this same plain int.
 */
int	errno;

/*
 * The values the stub serves. stub_version is longer than the version and
 * machine fields together, which is what carries a walk bounded by the
 * reported length past the whole structure rather than into the next field.
 */
static char stub_version[SYS_NMLN * 3];
static char stub_hostname[64];
static const char stub_short[] = "DiscoBSD";

/* Calls that reported truncation, so a test can require the case arose. */
static unsigned truncations;

/*
 * The symbols the CPU_NLIST double knows, each with the components that
 * spell it: four bytes to a word, the first in the low byte, a terminator
 * inside the last word.
 */
static const struct {
	u_int	words[3];
	u_int	count;
	u_int	value;
} symbols[] = {
	{ { 0x007a685fu }, 1, 0x20000100u },			/* "_hz" */
	{ { 0x6362615fu, 0 }, 2, 0x20000200u },			/* "_abc" */
	{ { 0x5f6b645fu, 0x6972646eu, 0x00006576u }, 3,
	    0x20000300u },					/* "_dk_ndrive" */
};

/* What the CPU_NLIST double saw. */
static unsigned nlist_calls, nlist_new_values;

static int
nlist_answer(int *words, u_int count, void *oldp, size_t *oldlenp,
    void *newp)
{
	unsigned i, j;

	nlist_calls++;
	if (newp != NULL)
		nlist_new_values++;
	for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
		if (symbols[i].count != count)
			continue;
		for (j = 0; j < count; j++)
			if ((u_int)words[j] != symbols[i].words[j])
				break;
		if (j < count)
			continue;
		if (*oldlenp < sizeof(symbols[i].value)) {
			errno = ENOMEM;
			return (-1);
		}
		memcpy(oldp, &symbols[i].value, sizeof(symbols[i].value));
		*oldlenp = sizeof(symbols[i].value);
		return (0);
	}
	errno = EOPNOTSUPP;
	return (-1);
}

static const char *
value_for(int level, int id)
{
	if (level == CTL_HW)
		return (stub_short);
	switch (id) {
	case KERN_HOSTNAME:
		return (stub_hostname);
	case KERN_OSVERSION:
	case KERN_VERSION:
		return (stub_version);
	default:
		return (stub_short);
	}
}

int
test_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	const char *val;
	size_t len;

	(void)newlen;
	if (namelen < 2) {
		errno = EINVAL;
		return (-1);
	}
	if (name[0] == CTL_MACHDEP && name[1] == CPU_NLIST)
		return (nlist_answer(name + 2, namelen - 2, oldp, oldlenp, newp));
	if (namelen > CTL_MAXNAME) {
		errno = EINVAL;
		return (-1);
	}
	val = value_for(name[0], name[1]);
	len = strlen(val) + 1;
	if (oldp != NULL && *oldlenp > 0)
		memcpy(oldp, val, MIN(len, *oldlenp));
	if (oldp != NULL && *oldlenp < len) {
		*oldlenp = len;
		truncations++;
		errno = ENOMEM;
		return (-1);
	}
	*oldlenp = len;
	return (0);
}

/*
 * uname() fills five fixed fields, and walks one of them -- version -- to
 * rewrite the newlines and tabs in it. The structure sits at the front of a
 * block whose remainder is a guard, and the guard is filled with the newline
 * the walk acts on, because a walk that runs past the structure rewrites only
 * the bytes it recognizes: a guard of some inert filler would survive the
 * overrun and report nothing.
 */
static struct {
	struct utsname	name;
	char		guard[512];
} area;

#define GUARD_BYTE	'\n'

static int
guard_intact(void)
{
	unsigned i;

	for (i = 0; i < sizeof(area.guard); i++)
		if (area.guard[i] != GUARD_BYTE)
			return (0);
	return (1);
}

static void
reset_area(void)
{
	memset(&area, 0, sizeof(area));
	memset(area.guard, GUARD_BYTE, sizeof(area.guard));
	truncations = 0;
}

static void
test_uname_stays_inside_its_fields(void)
{
	unsigned i;

	/*
	 * A version banner carrying the newlines uname() rewrites into
	 * spaces, which is the one place it walks a field's contents.
	 */
	for (i = 0; i < sizeof(stub_version) - 1; i++)
		stub_version[i] = (i % 16 == 15) ? '\n' : 'v';
	stub_version[sizeof(stub_version) - 1] = '\0';
	strcpy(stub_hostname, "a-host-name");

	reset_area();
	(void)uname(&area.name);

	CHECK(truncations > 0);		/* the fixture did exercise the case */
	CHECK(guard_intact());

	/* Every field is a string, whatever the value's length was. */
	CHECK(area.name.sysname[sizeof(area.name.sysname) - 1] == '\0');
	CHECK(area.name.nodename[sizeof(area.name.nodename) - 1] == '\0');
	CHECK(area.name.release[sizeof(area.name.release) - 1] == '\0');
	CHECK(area.name.version[sizeof(area.name.version) - 1] == '\0');
	CHECK(area.name.machine[sizeof(area.name.machine) - 1] == '\0');
	CHECK(strlen(area.name.version) < sizeof(area.name.version));
	CHECK(strlen(area.name.machine) < sizeof(area.name.machine));

	/* The machine field holds what HW_MACHINE reported and nothing else. */
	CHECK(strcmp(area.name.machine, stub_short) == 0);
}

/*
 * The same walk against a structure with nothing of its own behind it. The
 * guard above catches a write; this catches the read as well, because an
 * allocation of exactly sizeof(struct utsname) puts the sanitizer's redzone
 * at the first byte past the last field. Without a sanitizer the case still
 * runs and simply proves uname() fills the structure.
 */
static void
test_uname_on_an_exact_allocation(void)
{
	struct utsname *name;
	unsigned i;

	for (i = 0; i < sizeof(stub_version) - 1; i++)
		stub_version[i] = (i % 16 == 15) ? '\n' : 'v';
	stub_version[sizeof(stub_version) - 1] = '\0';
	strcpy(stub_hostname, "a-host-name");

	name = lsys_alloc(sizeof(*name));
	memset(name, 0, sizeof(*name));
	truncations = 0;

	(void)uname(name);

	CHECK(truncations > 0);
	CHECK(name->version[sizeof(name->version) - 1] == '\0');
	CHECK(strcmp(name->machine, stub_short) == 0);
	lsys_free(name);
}

/* A value that fits leaves every field holding it whole. */
static void
test_uname_short_values(void)
{
	strcpy(stub_version, "DiscoBSD 2.11 PICO");
	strcpy(stub_hostname, "pico");

	reset_area();
	CHECK(uname(&area.name) == 0);
	CHECK(truncations == 0);
	CHECK(guard_intact());
	CHECK(strcmp(area.name.nodename, "pico") == 0);
	CHECK(strcmp(area.name.machine, stub_short) == 0);
	CHECK(strcmp(area.name.sysname, stub_short) == 0);
}

/*
 * gethostname() hands its caller's buffer straight to sysctl(3), so a host
 * name longer than the buffer arrives as an unterminated prefix. POSIX names
 * ENAMETOOLONG for the condition and leaves termination unspecified, which
 * makes terminating it the safe reading of the same statement.
 */
static void
test_gethostname_truncation(void)
{
	char buf[8];
	unsigned i;

	strcpy(stub_hostname, "a-very-long-host-name");
	for (i = 0; i < sizeof(buf); i++)
		buf[i] = 'Z';
	errno = 0;
	CHECK(gethostname(buf, sizeof(buf)) == -1);
	CHECK(errno == ENAMETOOLONG);
	CHECK(buf[sizeof(buf) - 1] == '\0');
	CHECK(strlen(buf) < sizeof(buf));
	/* What arrived is the leading part of the name. */
	CHECK(strncmp(buf, stub_hostname, strlen(buf)) == 0);
}

/* A host name that fits arrives whole and reports success. */
static void
test_gethostname_fits(void)
{
	char buf[32];

	strcpy(stub_hostname, "pico");
	errno = 0;
	CHECK(gethostname(buf, sizeof(buf)) == 0);
	CHECK(strcmp(buf, "pico") == 0);
}

/* A zero-length buffer writes nothing at all. */
static void
test_gethostname_zero_length(void)
{
	char buf[4];
	unsigned i;

	strcpy(stub_hostname, "pico");
	for (i = 0; i < sizeof(buf); i++)
		buf[i] = 'Z';
	CHECK(gethostname(buf, 0) == -1);
	for (i = 0; i < sizeof(buf); i++)
		CHECK(buf[i] == 'Z');
}

/*
 * knlist() finds each symbol the double knows through its literal
 * components, passes no new value, and leaves 0 in an unknown entry and in
 * a name too long for CTL_MAXNAME components without asking for it.
 */
static void
test_knlist_spells_names(void)
{
	char longest[(CTL_MAXNAME - 2) * CTL_NLIST_BYTES];
	char overlong[sizeof(longest) + 1];
	struct nlist list[7];

	memset(longest, 'x', sizeof(longest) - 1);
	longest[sizeof(longest) - 1] = '\0';
	memset(overlong, 'x', sizeof(overlong) - 1);
	overlong[sizeof(overlong) - 1] = '\0';
	memset(list, 0, sizeof(list));
	list[0].n_name = "_hz";
	list[1].n_name = "_dk_ndrive";
	list[2].n_name = "_unknown";
	list[3].n_name = "_abc";
	list[4].n_name = longest;
	list[5].n_name = overlong;
	list[2].n_value = 7;
	list[5].n_value = 7;
	nlist_calls = 0;
	nlist_new_values = 0;

	CHECK(knlist(list) == 3);
	CHECK(list[0].n_value == 0x20000100u);
	CHECK(list[1].n_value == 0x20000300u);
	CHECK(list[2].n_value == 0);
	CHECK(list[3].n_value == 0x20000200u);
	CHECK(list[4].n_value == 0);
	CHECK(list[5].n_value == 0);
	/* The longest name fits the MIB and is asked; one byte more is not. */
	CHECK(nlist_calls == 5);
	CHECK(nlist_new_values == 0);
}

int
main(void)
{
	test_uname_stays_inside_its_fields();
	test_uname_on_an_exact_allocation();
	test_uname_short_values();
	test_gethostname_truncation();
	test_gethostname_fits();
	test_gethostname_zero_length();
	test_knlist_spells_names();
	return (lsys_verdict("libc_sysctl"));
}
