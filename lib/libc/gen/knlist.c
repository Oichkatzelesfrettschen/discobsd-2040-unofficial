#include <stdio.h>
#include <string.h>
#include <nlist.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>

/*
 * Set n_value of each entry of list to the address of the kernel symbol it
 * names, or to 0 for a symbol the kernel does not export or a name too long
 * for the MIB, and return the count found. {CTL_MACHDEP, CPU_NLIST, ...}
 * carries the name in its components as sys/sysctl.h defines at
 * CTL_NLIST_BYTES and takes no new value, so any user may call this.
 */
int
knlist(list)
	struct nlist *list;
{
	register struct nlist *p;
	int mib[CTL_MAXNAME], entries = 0;
	u_int namelen;
	size_t length, size, i;

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_NLIST;

	/*
	 * Clean out any left-over information for all valid entries.
	 * Type and value defined to be 0 if not found; historical
	 * versions cleared other and desc as well.
	 */
	for (p=list; p->n_name && p->n_name[0]; ++p) {
		p->n_value = 0;
		length = strlen(p->n_name);
		namelen = 2 + length / CTL_NLIST_BYTES + 1;
		if (namelen > CTL_MAXNAME)
			continue;
		for (i = 2; i < namelen; i++)
			mib[i] = 0;
		for (i = 0; i < length; i++)
			mib[2 + i / CTL_NLIST_BYTES] |= (int)((u_int)
			    (unsigned char)p->n_name[i] <<
			    (8 * (i % CTL_NLIST_BYTES)));
		size = sizeof(p->n_value);
		if (sysctl(mib, namelen, &p->n_value, &size, NULL, 0) < 0) {
			p->n_value = 0;
			continue;
		}
		++entries;
	}
	return entries;
}
