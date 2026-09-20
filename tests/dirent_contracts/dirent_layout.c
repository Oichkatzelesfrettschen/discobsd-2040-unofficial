#include <sys/types.h>
#include <sys/dir.h>
#include <stddef.h>

_Static_assert(sizeof(DIR) == 1040,
    "the RP2040 directory stream must occupy 1040 bytes");
_Static_assert(offsetof(DIR, dd_fd) == 0,
    "the directory descriptor must remain the first field");
_Static_assert(offsetof(DIR, dd_seek) == 4,
    "the directory buffer cookie must follow the descriptor");
_Static_assert(offsetof(DIR, dd_loc) == 8,
    "the directory buffer index must follow the cookie");
_Static_assert(offsetof(DIR, dd_size) == 12,
    "the directory byte count must follow the index");
_Static_assert(offsetof(DIR, dd_buf) == 16,
    "the directory data must follow the scalar state");
_Static_assert(sizeof(((DIR *)0)->dd_buf) == DIRBLKSIZ,
    "the directory stream must retain one complete directory block");

int
dirent_layout_contract(void)
{
	return 0;
}
