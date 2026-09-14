#include "../../include/stdio.h"
#include "../../include/stdlib.h"

typedef char tmpnam_buffer_must_hold_template[
    L_tmpnam >= sizeof(P_tmpdir "XXXXXX") ? 1 : -1];

FILE *(*tmpfile_signature)(void) = tmpfile;
char *(*tmpnam_signature)(char [L_tmpnam]) = tmpnam;
char *(*tempnam_signature)(const char *, const char *) = tempnam;
char *(*mktemp_signature)(char *) = mktemp;
int (*mkstemp_signature)(char *) = mkstemp;
