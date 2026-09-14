#include "../../include/stdio.h"

typedef char ctermid_buffer_must_hold_path[
    L_ctermid >= sizeof("/dev/tty") ? 1 : -1];
