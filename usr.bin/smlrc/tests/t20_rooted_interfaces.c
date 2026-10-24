/*
 * Native Smaller C acceptance for rooted creation interfaces.
 * The separate translation unit keeps the native compiler's fixed symbol
 * table from coupling the interface contract to the broad stdio fixture.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <fcntl.h>
#include <unistd.h>
#include <unistd.h>

static int (*openat_beneath_signature)(int, const char *, int, ...) =
    openat_beneath;
static int (*mkdirat_beneath_signature)(int, const char *, mode_t) =
    mkdirat_beneath;
static int (*openat_signature)(int, const char *, int, ...) = openat;
static int (*unlinkat_beneath_signature)(int, const char *, int) =
    unlinkat_beneath;
static int (*linkat_beneath_signature)(int, const char *, int,
    const char *) = linkat_beneath;

int
main(void)
{
    static const char success[] = "rooted interfaces\n";

    if (openat_beneath_signature == NULL)
        return 1;
    if (mkdirat_beneath_signature == NULL)
        return 2;
    if (openat_signature == NULL)
        return 3;
    if (unlinkat_beneath_signature == NULL)
        return 4;
    if (linkat_beneath_signature == NULL)
        return 5;
    {
        int (*publishat_signature)(int, const char *, int, const char *, int) =
            publishat_beneath;

        if (publishat_signature == NULL)
            return 6;
    }
    if (write(STDOUT_FILENO, success, sizeof(success) - 1) !=
        (ssize_t)(sizeof(success) - 1))
        return 7;
    return 0;
}
