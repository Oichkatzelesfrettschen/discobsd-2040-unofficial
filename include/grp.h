/*  grp.h   4.1 83/05/03    */

#ifndef _GRP_H_
#define _GRP_H_

#include <sys/types.h>

struct  group {         /* see getgrent(3) */
    char    *gr_name;
    char    *gr_passwd;
    int     gr_gid;
    char    **gr_mem;
};

struct group *getgrent(void);
struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);
void setgrent(void);
void endgrent(void);
int setgroups(size_t size, const gid_t *list);

#endif /* _GRP_H_ */
