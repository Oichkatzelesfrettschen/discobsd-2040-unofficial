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
/*
 * Host gate for fdcloseexec() in kern_descrip.c, which execve() runs from
 * exec_finish() to close the close-on-exec descriptors once the executable's
 * inode is released.
 *
 * A close routine can sleep (a terminal draining stopped output), and an
 * exit() that interrupts it walks u_ofile and closes whatever it still finds.
 * The fixture's close routine therefore reports, at the moment the kernel
 * reaches it, whether the descriptor slot of the file being closed still
 * holds that file and still carries UF_EXCLOSE. The slot must be empty with
 * flags 0 by then. The cases also pin which slots a call touches, the
 * u_lastfile high-water mark it leaves, and the reference count of a file
 * that two descriptors share.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#ifdef HK_ILP32
_Static_assert(sizeof(int) == 4, "int is the target's four bytes");
_Static_assert(sizeof(void *) == 4, "pointers are the target's four bytes");
#endif

#define SLOTS 5

struct user u;
struct timeval time;
int lbolt;
int usechz;

static struct file files[SLOTS];

/* What the close routine saw, indexed by the file's position in files[]. */
static unsigned int close_calls[SLOTS];
static unsigned int slot_still_held[SLOTS];
static unsigned int flags_still_set[SLOTS];
static unsigned int closes_total;

static int
file_index(const struct file *fp)
{
	int i;

	for (i = 0; i < SLOTS; i++)
		if (fp == &files[i])
			return i;
	return -1;
}

static int
stub_rw(struct file *file, struct uio *uio)
{
	(void)file;
	(void)uio;
	return EOPNOTSUPP;
}

static int
stub_ioctl(struct file *file, u_int command, char *data)
{
	(void)file;
	(void)command;
	(void)data;
	return EOPNOTSUPP;
}

static int
stub_select(struct file *file, int flag)
{
	(void)file;
	(void)flag;
	return 0;
}

static int
observing_close(struct file *file)
{
	int index = file_index(file);
	int slot;

	closes_total++;
	if (index < 0)
		return 0;
	close_calls[index]++;
	for (slot = 0; slot < NOFILE; slot++) {
		if (u.u_ofile[slot] == file)
			slot_still_held[index]++;
		if (u.u_ofile[slot] == file && (u.u_pofile[slot] & UF_EXCLOSE))
			flags_still_set[index]++;
	}
	return 0;
}

const struct fileops inodeops = {
	stub_rw, stub_ioctl, stub_select, observing_close
};
const struct fileops pipeops = {
	stub_rw, stub_ioctl, stub_select, observing_close
};

const struct fileops *const Fops[] = {
	NULL, &inodeops, NULL, &pipeops
};

struct proc *
pfind(int pid)
{
	(void)pid;
	return NULL;
}

int
baduaddr_span(caddr_t address, u_int nbytes)
{
	(void)address;
	(void)nbytes;
	return 1;
}

int
baduaddr(caddr_t address)
{
	(void)address;
	return 1;
}

int
baduaddr_rspan(caddr_t address, u_int nbytes)
{
	(void)address;
	(void)nbytes;
	return 1;
}

int
copyin(const caddr_t from, caddr_t to, u_int nbytes)
{
	(void)from;
	(void)to;
	(void)nbytes;
	return EFAULT;
}

int
copyout(const caddr_t from, caddr_t to, u_int nbytes)
{
	(void)from;
	(void)to;
	(void)nbytes;
	return EFAULT;
}

int
ino_lock(struct file *fp, int cmd)
{
	(void)fp;
	(void)cmd;
	return 0;
}

void
ino_unlock(struct file *fp, int kind)
{
	(void)fp;
	(void)kind;
}

/* Slots 0 to SLOTS - 1 hold distinct open files; mask bit n marks slot n. */
static void
setup(unsigned int exclose_mask)
{
	int slot;

	bzero(&u, sizeof u);
	bzero(files, sizeof files);
	bzero(close_calls, sizeof close_calls);
	bzero(slot_still_held, sizeof slot_still_held);
	bzero(flags_still_set, sizeof flags_still_set);
	closes_total = 0;
	for (slot = 0; slot < SLOTS; slot++) {
		files[slot].f_count = 1;
		files[slot].f_type = DTYPE_INODE;
		u.u_ofile[slot] = &files[slot];
		if (exclose_mask & (1u << slot))
			u.u_pofile[slot] = UF_EXCLOSE;
	}
	u.u_lastfile = SLOTS - 1;
}

/*
 * Close the slots in mask and require that exactly those files reach their
 * close routine, each once, with its slot already empty, that every other
 * slot is untouched, and that u_lastfile lands on the highest slot left.
 */
static void
run_case(unsigned int mask, int expected_lastfile)
{
	int slot;

	setup(mask);
	fdcloseexec();
	for (slot = 0; slot < SLOTS; slot++) {
		if (mask & (1u << slot)) {
			HK_CHECK(close_calls[slot] == 1);
			HK_CHECK(slot_still_held[slot] == 0);
			HK_CHECK(flags_still_set[slot] == 0);
			HK_CHECK(u.u_ofile[slot] == NULL);
			HK_CHECK(u.u_pofile[slot] == 0);
			HK_CHECK(files[slot].f_count == 0);
		} else {
			HK_CHECK(close_calls[slot] == 0);
			HK_CHECK(u.u_ofile[slot] == &files[slot]);
			HK_CHECK(u.u_pofile[slot] == 0);
			HK_CHECK(files[slot].f_count == 1);
		}
	}
	HK_CHECK(u.u_lastfile == expected_lastfile);
}

static void
lastfile_cases(void)
{
	run_case(0x00, 4);	/* none: nothing closes, mark stays */
	run_case(0x10, 3);	/* the last slot only */
	run_case(0x04, 4);	/* a middle slot leaves the mark */
	run_case(0x18, 2);	/* a run at the end */
	run_case(0x1f, -1);	/* all */
	run_case(0x01, 4);	/* the first slot only */
	run_case(0x0f, 4);	/* all but the last: the mark stays */
}

/* A gap below the mark drops with the slots above it. */
static void
gap_case(void)
{
	setup(0x10);
	u.u_ofile[3] = NULL;
	fdcloseexec();
	HK_CHECK(close_calls[4] == 1);
	HK_CHECK(u.u_lastfile == 2);
}

/* A slot flagged for close with no file in it never reaches a close routine. */
static void
empty_slot_case(void)
{
	setup(0x04);
	u.u_ofile[2] = NULL;
	fdcloseexec();
	HK_CHECK(closes_total == 0);
	HK_CHECK(u.u_pofile[2] == 0);
	HK_CHECK(u.u_lastfile == 4);
}

/* Other per-descriptor flag bits go with the slot that carries them. */
static void
other_bits_case(void)
{
	setup(0x02);
	u.u_pofile[2] = 0x6;
	fdcloseexec();
	HK_CHECK(u.u_pofile[2] == 0x6 && u.u_ofile[2] == &files[2]);
	HK_CHECK(u.u_pofile[1] == 0 && u.u_ofile[1] == NULL);
}

/*
 * Two descriptors on one file: closing one only drops the count; the close
 * routine runs when the last descriptor goes.
 */
static void
shared_cases(void)
{
	setup(0x03);
	u.u_ofile[1] = &files[0];
	files[0].f_count = 2;
	fdcloseexec();
	HK_CHECK(close_calls[0] == 1);
	HK_CHECK(slot_still_held[0] == 0);
	HK_CHECK(files[0].f_count == 0);
	HK_CHECK(u.u_ofile[0] == NULL && u.u_ofile[1] == NULL);

	setup(0x01);
	u.u_ofile[1] = &files[0];
	files[0].f_count = 2;
	fdcloseexec();
	HK_CHECK(closes_total == 0);
	HK_CHECK(files[0].f_count == 1);
	HK_CHECK(u.u_ofile[0] == NULL && u.u_ofile[1] == &files[0]);
	HK_CHECK(u.u_pofile[0] == 0 && u.u_pofile[1] == 0);
}

int
main(void)
{
	lastfile_cases();
	gap_case();
	empty_slot_case();
	other_bits_case();
	shared_cases();
	return hk_verdict("exec close-on-exec descriptor close");
}
