#include "../../include/stdio.h"
#include "../../include/stdlib.h"
#include "../../include/string.h"
#include "../../include/strings.h"
#include "../../include/time.h"
#include "../../include/vis.h"

typedef char ctermid_buffer_must_hold_path[
    L_ctermid >= sizeof("/dev/tty") ? 1 : -1];

static char *(*const bounded_vis_signature)(char *, size_t, int, int, int) =
    nvis;
static int (*const bounded_strvis_signature)(char *, size_t, const char *,
    int) = strnvis;
static int (*const bounded_strvisx_signature)(char *, size_t, const char *,
    size_t, int) = strnvisx;
static void (*const explicit_bzero_signature)(void *, size_t) =
    explicit_bzero;
static void *(*const reallocarray_signature)(void *, size_t, size_t) =
    reallocarray;
static void *(*const memmem_signature)(const void *, size_t, const void *,
    size_t) = memmem;
static ssize_t (*const getdelim_signature)(char **, size_t *, int, FILE *) =
    getdelim;
static ssize_t (*const getline_signature)(char **, size_t *, FILE *) =
    getline;
static int (*const timingsafe_bcmp_signature)(const void *, const void *,
    size_t) = timingsafe_bcmp;
static char *(*const timezone_signature)(int, int) = timezone;
static void (*const tzset_signature)(void) = tzset;

void
vis_header_contract(void)
{
    (void)bounded_vis_signature;
    (void)bounded_strvis_signature;
    (void)bounded_strvisx_signature;
    (void)explicit_bzero_signature;
    (void)reallocarray_signature;
    (void)memmem_signature;
    (void)getdelim_signature;
    (void)getline_signature;
    (void)timingsafe_bcmp_signature;
    (void)timezone_signature;
    (void)tzset_signature;
}
