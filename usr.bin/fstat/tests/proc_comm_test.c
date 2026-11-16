#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Mirrors of sys/sys/sysctl.h. proc_comm_gate.py reads that header and fails
 * when one of these fields or values changes there.
 */
#define KI_COMMSZ	64
#define KP_VALID_COMM	0x20

struct kinfo_proc {
	uint16_t	kp_valid;
	int16_t		kp_pid;
	char		kp_comm[KI_COMMSZ];
};

/* PROC_COMM_SRC names the extracted copy of proc_comm() from fstat.c. */
#include PROC_COMM_SRC

static int failures;

static void
expect(const struct kinfo_proc *kp, const char *want, const char *what)
{
	const char *got = proc_comm(kp);

	if (strcmp(got, want) != 0) {
		fprintf(stderr, "proc_comm: %s: want \"%s\", got \"%s\"\n",
		    what, want, got);
		failures++;
	}
}

int
main(void)
{
	struct kinfo_proc kp;

	memset(&kp, 0, sizeof(kp));
	kp.kp_valid = KP_VALID_COMM;
	kp.kp_pid = 0;
	expect(&kp, "swapper", "pid 0 with an empty name");

	memset(&kp, 0, sizeof(kp));
	kp.kp_pid = 0;
	expect(&kp, "-", "pid 0 without KP_VALID_COMM");

	memset(&kp, 0, sizeof(kp));
	kp.kp_valid = KP_VALID_COMM;
	kp.kp_pid = 1;
	strcpy(kp.kp_comm, "init");
	expect(&kp, "init", "named process 1");

	if (failures != 0)
		return 1;
	puts("proc_comm: 3 records named as required");
	return 0;
}
