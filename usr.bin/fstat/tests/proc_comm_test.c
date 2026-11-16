/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
