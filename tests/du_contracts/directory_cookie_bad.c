#include <dirent.h>

void
resume_with_foreign_cookie(DIR *first, DIR *second)
{
	long location = telldir(first);

	seekdir(second, location);
}
