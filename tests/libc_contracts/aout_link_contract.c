#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <vis.h>

void (*aout_context_sort)(void *, size_t, size_t,
    int (*)(const void *, const void *, void *), void *) = qsort_r;
void (*aout_plain_sort)(void *, size_t, size_t,
    int (*)(const void *, const void *)) = qsort;
volatile double aout_contract_elapsed;
void (*aout_quick_exit)(int) = quick_exit;
void (*aout_immediate_exit)(int) = _Exit;
int (*aout_register_quick_exit)(void (*)(void)) = at_quick_exit;

int
aout_contract_probe(void)
{
	char terminal_name[L_ctermid];
	char visual[8];
	char runtime_path[32];
	char secret[4] = { 1, 2, 3, 4 };
	char bounded[8];
	char *duplicate = strndup("abc", 2);
	int string_contract = strnlen("abc", 2) == 2 &&
	    stpcpy(bounded, "abc") == bounded + 3 &&
	    stpncpy(bounded, "a", sizeof(bounded)) == bounded + 1 &&
	    duplicate != NULL;
	free(duplicate);

	explicit_bzero(secret, sizeof(secret));
	aout_contract_elapsed = difftime((time_t)1, (time_t)0);
	return string_contract + (ctermid(terminal_name) == terminal_name) + raise(0) +
	    confstr(_CS_PATH, runtime_path, sizeof(runtime_path)) +
	    sysconf(_SC_OPEN_MAX) +
	    (nvis(visual, sizeof(visual), '\n', VIS_CSTYLE, '\0') != visual) +
	    strnvisx(visual, sizeof(visual), "\0", 1, VIS_CSTYLE) +
	    timingsafe_bcmp(secret, "\0\0\0\0", sizeof(secret));
}
