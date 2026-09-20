#include "../../include/stdio.h"
#include "../../include/vis.h"

typedef char ctermid_buffer_must_hold_path[
    L_ctermid >= sizeof("/dev/tty") ? 1 : -1];

static char *(*const bounded_vis_signature)(char *, size_t, int, int, int) =
    nvis;
static int (*const bounded_strvis_signature)(char *, size_t, const char *,
    int) = strnvis;
static int (*const bounded_strvisx_signature)(char *, size_t, const char *,
    size_t, int) = strnvisx;

void
vis_header_contract(void)
{
    (void)bounded_vis_signature;
    (void)bounded_strvis_signature;
    (void)bounded_strvisx_signature;
}
