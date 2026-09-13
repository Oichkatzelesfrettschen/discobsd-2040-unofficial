#include <sys/types.h>
#include <grp.h>

struct group *
getgrgid(gid)
        register gid_t gid;
{
	register struct group *p;

	setgrent();
	while ((p = getgrent()) && (gid_t)p->gr_gid != gid);
	endgrent();
	return(p);
}
