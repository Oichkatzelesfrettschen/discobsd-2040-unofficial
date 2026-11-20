/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include "captest.h"

/* Read the credential prefix of each record before the first child runs. */
static int
owned_processes(uid_t owner)
{
	struct prefix {
		uint16_t size, valid;
		uint32_t euid, ruid;
	} *rows;
	int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0, sizeof(*rows) };
	size_t len, i;
	int count = 0;

	_Static_assert(offsetof(struct prefix, ruid) == offsetof(struct kinfo_proc, kp_ruid),
	    "process credential prefix must match the kernel record");
	if (sysctl(mib, 5, NULL, &len, NULL, 0) < 0)
		return -1;
	rows = malloc(len);
	if (rows == NULL)
		return -1;
	if (sysctl(mib, 5, rows, &len, NULL, 0) < 0 || len % sizeof(*rows)) {
		free(rows);
		return -1;
	}
	for (i = 0; i < len / sizeof(*rows); i++) {
		if (!(rows[i].valid & KP_VALID_CRED)) {
			count = -1;
			break;
		}
		if (rows[i].ruid == owner)
			count++;
	}
	free(rows);
	return count;
}

int
main(void)
{
	struct kernel_capacity_stats before, full;
	struct cap_reaped reaped;
	pid_t child;
	int initial, kids, status, refusal;
	long limit = sysconf(_SC_CHILD_MAX);

	initial = owned_processes(getuid());
	if (geteuid() == 0 || limit < 25 || limit > CAP_MAXKIDS ||
	    initial < 1 || initial >= limit ||
	    cap_snapshot(&before) < 0 || cap_hold_setup() < 0) {
		cap_say("uidquota setup: limit %ld owned %d errno %d", limit, initial, errno);
		return 1;
	}
	for (;;) {
		child = fork();
		if (child == 0)
			cap_hold_park();
		if (child < 0)
			break;
		if (cap_kid_add(child) < 0) {
			kill(child, SIGKILL);
			waitpid(child, &status, 0);
			cap_abort_all();
			return 1;
		}
	}
	refusal = errno;
	kids = cap_kid_count();
	reaped = cap_reap(0);
	if (cap_snapshot(&full) < 0 || refusal != EAGAIN ||
	    initial + kids != limit || reaped.live != kids ||
	    full.metric[CAPACITY_PROC].current + 1 >= full.metric[CAPACITY_PROC].limit ||
	    full.metric[CAPACITY_PROC].current != before.metric[CAPACITY_PROC].current + kids) {
		cap_say("uidquota refusal: owned %d children %d live %d errno %d",
		    initial, kids, reaped.live, refusal);
		cap_abort_all();
		return 1;
	}
	cap_release();
	reaped = cap_reap(CAP_SETTLE_SECONDS);
	if (reaped.clean != kids) {
		cap_abort_all();
		return 1;
	}
	child = fork();
	if (child == 0)
		_exit(0);
	if (child < 0 || waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return 1;
	cap_say("uidquota-pass");
	return 0;
}
