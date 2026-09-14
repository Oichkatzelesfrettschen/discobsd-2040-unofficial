#include <sys/types.h>

#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

uid_t
test_getuid(void)
{
	return 10;
}

uid_t
test_geteuid(void)
{
	return 20;
}

gid_t
test_getgid(void)
{
	return 30;
}

gid_t
test_getegid(void)
{
	return 40;
}

char *
test_getlogin(void)
{
	if (getenv("TEST_GETLOGIN_FAIL") != NULL)
		return NULL;
	return (char *)"session-login";
}

static struct passwd *
password_entry(const char *name, uid_t user_id, gid_t group_id)
{
	static struct passwd entry;

	memset(&entry, 0, sizeof(entry));
	entry.pw_name = (char *)name;
	entry.pw_uid = user_id;
	entry.pw_gid = group_id;
	return &entry;
}

struct passwd *
test_getpwuid(uid_t user_id)
{
	if (user_id == 10)
		return password_entry("real-user", 10, 30);
	if (user_id == 20)
		return password_entry("effective-user", 20, 40);
	if (user_id == 100)
		return password_entry("alice", 100, 60);
	return NULL;
}

struct passwd *
test_getpwnam(const char *name)
{
	if (strcmp(name, "alice") == 0)
		return password_entry("alice", 100, 60);
	return NULL;
}

static struct group *
group_entry(const char *name, gid_t group_id)
{
	static char *members[] = { NULL };
	static struct group entry;

	memset(&entry, 0, sizeof(entry));
	entry.gr_name = (char *)name;
	entry.gr_gid = group_id;
	entry.gr_mem = members;
	return &entry;
}

struct group *
test_getgrgid(gid_t group_id)
{
	if (group_id == 30)
		return group_entry("primary", 30);
	if (group_id == 40)
		return group_entry("effective-group", 40);
	if (group_id == 50)
		return group_entry("extra", 50);
	if (group_id == 60)
		return group_entry("staff", 60);
	return NULL;
}

int
test_getgroups(int capacity, gid_t *groups)
{
	if (capacity < 2)
		return -1;
	groups[0] = 30;
	groups[1] = 50;
	return 2;
}

int
test_getgrouplist(const char *name, gid_t base_group, gid_t *groups,
    int *group_count)
{
	if (strcmp(name, "alice") != 0 || base_group != 60 || *group_count < 2)
		return -1;
	groups[0] = 60;
	groups[1] = 50;
	*group_count = 2;
	return 0;
}
