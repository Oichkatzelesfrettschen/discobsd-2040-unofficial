/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define _SYS_TYPES_H_
#include "../../sys/sys/exec_elf.h"

#ifndef EXEC_ELF_FUNCTION
#define EXEC_ELF_FUNCTION "exec_elf_admission_function.c"
#endif

typedef char *caddr_t;

#define DEBUG(...) ((void)0)
#define ELF32_MACHDEP_ID EM_ARM
#define ENOEXEC 8
#define ENOMEM 12
#define EIO 5
#define IO_UNIT 0
#define UIO_READ 0
#define SIGKILL 9
#define SSIZE 2048U
#define NBPW 4U
#define USER_BASE 0x20000000U
#define USER_WINDOW (144U * 1024U)
#define USER_WINDOW_MAX USER_WINDOW
#define NO_ADDR ((caddr_t)(uintptr_t)UINT_MAX)
#define __user_data_start ((caddr_t)(uintptr_t)USER_BASE)
#define USER_TOP(process_pointer) (USER_BASE + USER_WINDOW)
#define roundup(value, alignment) \
	(((value) + (alignment) - 1U) & ~((alignment) - 1U))
#define __thumb__ 1

struct proc {
	int p_pid;
};

struct inode {
	off_t i_size;
};

struct memsect {
	caddr_t vaddr;
	unsigned int len;
};

struct exec_params {
	union {
		struct elf_ehdr elf;
	} hdr;
	int hdr_len;
	unsigned short argc;
	unsigned short envc;
	unsigned short argbc;
	unsigned short envbc;
	struct inode *ip;
	struct memsect text;
	struct memsect data;
	struct memsect bss;
	struct memsect heap;
	struct memsect stack;
};

struct user {
	struct proc *u_procp;
};

static struct user u;
static struct proc process;
static struct inode executable_inode;
static struct elf_phdr supplied_program_header;
static unsigned char allocation[sizeof(struct elf_phdr) * 2U];
static int allocation_calls;
static int allocated_bytes;
static int read_calls;
static int program_header_reads;
static int segment_reads;
static off_t segment_read_offset;
static int segment_read_length;
static int program_header_read_error;
static int program_header_residual;
static int segment_read_error;
static int segment_read_residual;
static int save_args_calls;
static int save_args_error;
static int establish_calls;
static int establish_error;
static int clear_calls;
static int setup_calls;
static unsigned int setup_entry;
static int signal_calls;
static int signaled_number;
static int failures;

static void *
exec_alloc(int size, int alignment, struct exec_params *parameters)
{
	(void)alignment;
	(void)parameters;
	allocation_calls++;
	allocated_bytes = size;
	if (size < 0 || (size_t)size > sizeof(allocation))
		return NULL;
	return allocation;
}

static int
rdwri(int operation, struct inode *inode, caddr_t destination, int length,
    off_t offset, int flags, int *residual)
{
	(void)operation;
	(void)inode;
	(void)flags;
	read_calls++;
	if (destination == (caddr_t)allocation) {
		program_header_reads++;
		if (residual != NULL)
			*residual = program_header_residual;
		if (program_header_read_error == 0)
			memcpy(destination, &supplied_program_header,
			    sizeof(supplied_program_header));
		return program_header_read_error;
	}
	segment_reads++;
	segment_read_offset = offset;
	segment_read_length = length;
	if (residual != NULL)
		*residual = segment_read_residual;
	return segment_read_error;
}

static int
exec_save_args(struct exec_params *parameters)
{
	save_args_calls++;
	parameters->argc = 1;
	parameters->envc = 1;
	parameters->argbc = 3;
	parameters->envbc = 4;
	return save_args_error;
}

static int
exec_estab(struct exec_params *parameters)
{
	(void)parameters;
	establish_calls++;
	return establish_error;
}

static void
exec_clear(struct exec_params *parameters)
{
	(void)parameters;
	clear_calls++;
}

static void
exec_setupstack(unsigned int entry, struct exec_params *parameters)
{
	(void)parameters;
	setup_calls++;
	setup_entry = entry;
}

static void
psignal(struct proc *target, int signal_number)
{
	(void)target;
	signal_calls++;
	signaled_number = signal_number;
}

#include EXEC_ELF_FUNCTION

#define CHECK(label, expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL %s\n", label); \
		failures++; \
	} \
} while (0)

static void
reset_fixture(struct exec_params *parameters)
{
	static const unsigned char elf_ident[9] = {
		ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
		ELFCLASS32, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV, 0
	};

	memset(parameters, 0, sizeof(*parameters));
	memset(allocation, 0, sizeof(allocation));
	memset(&supplied_program_header, 0, sizeof(supplied_program_header));
	memcpy(parameters->hdr.elf.e_ident, elf_ident, sizeof(elf_ident));
	parameters->hdr.elf.e_type = ET_EXEC;
	parameters->hdr.elf.e_machine = EM_ARM;
	parameters->hdr.elf.e_version = EV_CURRENT;
	parameters->hdr.elf.e_entry = USER_BASE + 1U;
	parameters->hdr.elf.e_phoff = sizeof(struct elf_ehdr);
	parameters->hdr.elf.e_ehsize = sizeof(struct elf_ehdr);
	parameters->hdr.elf.e_phentsize = sizeof(struct elf_phdr);
	parameters->hdr.elf.e_phnum = 1;
	parameters->hdr.elf.e_shentsize = sizeof(struct elf_shdr);
	parameters->hdr.elf.e_shnum = 1;
	parameters->hdr_len = sizeof(struct elf_ehdr);
	parameters->ip = &executable_inode;
	executable_inode.i_size = 0x400;
	supplied_program_header.p_type = PT_LOAD;
	supplied_program_header.p_offset = 0x100;
	supplied_program_header.p_vaddr = USER_BASE;
	supplied_program_header.p_paddr = USER_BASE;
	supplied_program_header.p_filesz = 0x80;
	supplied_program_header.p_memsz = 0x100;
	supplied_program_header.p_flags = PF_R | PF_W | PF_X;
	supplied_program_header.p_align = 4;
	u.u_procp = &process;
	allocation_calls = 0;
	allocated_bytes = 0;
	read_calls = 0;
	program_header_reads = 0;
	segment_reads = 0;
	segment_read_offset = 0;
	segment_read_length = 0;
	program_header_read_error = 0;
	program_header_residual = 0;
	segment_read_error = 0;
	segment_read_residual = 0;
	save_args_calls = 0;
	save_args_error = 0;
	establish_calls = 0;
	establish_error = 0;
	clear_calls = 0;
	setup_calls = 0;
	setup_entry = 0;
	signal_calls = 0;
	signaled_number = 0;
}

static void
check_valid_segment(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	CHECK("valid ELF succeeds", exec_elf_check(&parameters) == 0);
	CHECK("valid ELF reads one program header", program_header_reads == 1);
	CHECK("valid ELF allocates one program header",
	    allocation_calls == 1 &&
	    allocated_bytes == (int)sizeof(struct elf_phdr));
	CHECK("valid ELF saves arguments before commit", save_args_calls == 1);
	CHECK("valid ELF commits once", establish_calls == 1);
	CHECK("valid ELF publishes file-backed data",
	    (uintptr_t)parameters.data.vaddr == USER_BASE &&
	    parameters.data.len == 0x80U);
	CHECK("valid ELF publishes BSS",
	    (uintptr_t)parameters.bss.vaddr == USER_BASE + 0x80U &&
	    parameters.bss.len == 0x80U);
	CHECK("valid ELF publishes empty heap after BSS",
	    (uintptr_t)parameters.heap.vaddr == USER_BASE + 0x100U &&
	    parameters.heap.len == 0U);
	CHECK("valid ELF rounds argument bytes in stack extent",
	    parameters.stack.len == SSIZE + 8U + 6U * NBPW &&
	    (uintptr_t)parameters.stack.vaddr ==
	    USER_TOP(&process) - parameters.stack.len);
	CHECK("valid ELF reads one segment", segment_reads == 1);
	CHECK("valid ELF uses admitted file extent",
	    segment_read_offset == 0x100 && segment_read_length == 0x80);
	CHECK("valid ELF clears and publishes stack", clear_calls == 1 &&
	    setup_calls == 1 && setup_entry == USER_BASE + 1U);
	CHECK("valid ELF emits no signal", signal_calls == 0);
}

static void
check_bad_header_geometry(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	parameters.hdr.elf.e_phnum = 2;
	CHECK("multiple program headers are rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("bad program-header count skips allocation and read",
	    allocation_calls == 0 && read_calls == 0);

	reset_fixture(&parameters);
	executable_inode.i_size = -1;
	CHECK("negative inode size is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("negative inode size skips allocation and read",
	    allocation_calls == 0 && read_calls == 0);
}

static void
check_zero_bss_segment(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	supplied_program_header.p_memsz = supplied_program_header.p_filesz;
	CHECK("zero-BSS load succeeds", exec_elf_check(&parameters) == 0);
	CHECK("zero-BSS load commits once", establish_calls == 1);
	CHECK("zero-BSS load publishes image", clear_calls == 1 && setup_calls == 1);
}

static void
check_bad_memory_geometry(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	supplied_program_header.p_memsz = supplied_program_header.p_filesz - 1U;
	CHECK("file bytes beyond memory are rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("bad memory geometry preserves old image", establish_calls == 0);
	CHECK("bad memory geometry skips segment read", segment_reads == 0);

	reset_fixture(&parameters);
	supplied_program_header.p_memsz = USER_WINDOW + 1U;
	CHECK("segment beyond process window is rejected",
	    exec_elf_check(&parameters) == ENOMEM);
	CHECK("oversized segment preserves old image", establish_calls == 0);
}

static void
check_bad_segment_shape(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	supplied_program_header.p_type = PT_NOTE;
	CHECK("non-load segment is rejected", exec_elf_check(&parameters) == ENOEXEC);
	CHECK("non-load segment preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	supplied_program_header.p_flags = PF_R | PF_X;
	CHECK("non-flat permissions are rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("bad permissions preserve old image", establish_calls == 0);

	reset_fixture(&parameters);
	supplied_program_header.p_vaddr = USER_BASE + NBPW;
	CHECK("segment away from process base is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("bad segment base preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	supplied_program_header.p_filesz = 0;
	CHECK("empty file-backed segment is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("empty segment preserves old image", establish_calls == 0);
}

static void
check_bad_file_geometry(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	executable_inode.i_size = supplied_program_header.p_offset +
	    supplied_program_header.p_filesz - 1U;
	CHECK("truncated segment is rejected", exec_elf_check(&parameters) == ENOEXEC);
	CHECK("truncated segment preserves old image", establish_calls == 0);
	CHECK("truncated segment skips segment read", segment_reads == 0);

	reset_fixture(&parameters);
	supplied_program_header.p_offset = UINT_MAX - 0x40U;
	supplied_program_header.p_filesz = 0x80U;
	supplied_program_header.p_memsz = 0x80U;
	CHECK("wrapped segment file span is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("wrapped segment span preserves old image",
	    establish_calls == 0 && segment_reads == 0);

	reset_fixture(&parameters);
	parameters.hdr.elf.e_phoff = UINT_MAX -
	    (unsigned int)sizeof(struct elf_phdr) + 1U;
	CHECK("wrapped program-header table is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("wrapped table skips allocation and read",
	    allocation_calls == 0 && read_calls == 0);

	reset_fixture(&parameters);
	executable_inode.i_size = parameters.hdr.elf.e_phoff +
	    (off_t)sizeof(struct elf_phdr) - 1;
	CHECK("truncated program-header table is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("truncated table skips allocation and read",
	    allocation_calls == 0 && read_calls == 0);
}

static void
check_bad_entry(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	parameters.hdr.elf.e_entry = USER_BASE;
	CHECK("even Thumb entry is rejected", exec_elf_check(&parameters) == ENOEXEC);
	CHECK("even entry preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	parameters.hdr.elf.e_entry = USER_BASE +
	    supplied_program_header.p_filesz + 1U;
	CHECK("entry beyond file-backed bytes is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("outside entry preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	parameters.hdr.elf.e_entry = USER_BASE - 1U;
	CHECK("entry below segment is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("entry below segment preserves old image", establish_calls == 0);
}

static void
check_read_failures(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	program_header_read_error = EIO;
	CHECK("program-header read error is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("header read error preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	program_header_residual = 1;
	CHECK("short program-header read is rejected",
	    exec_elf_check(&parameters) == ENOEXEC);
	CHECK("short header preserves old image", establish_calls == 0);

	reset_fixture(&parameters);
	segment_read_error = EIO;
	CHECK("committed segment read error is returned",
	    exec_elf_check(&parameters) == EIO);
	CHECK("committed read error kills process",
	    signal_calls == 1 && signaled_number == SIGKILL);
	CHECK("read error withholds publication", clear_calls == 0 && setup_calls == 0);

	reset_fixture(&parameters);
	segment_read_residual = 1;
	CHECK("committed short read reports I/O failure",
	    exec_elf_check(&parameters) == EIO);
	CHECK("committed short read kills process",
	    signal_calls == 1 && signaled_number == SIGKILL);
	CHECK("short read withholds publication", clear_calls == 0 && setup_calls == 0);
}

static void
check_precommit_failures(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	save_args_error = EIO;
	CHECK("argument capture error is returned",
	    exec_elf_check(&parameters) == EIO);
	CHECK("argument capture error preserves old image",
	    establish_calls == 0 && segment_reads == 0 && signal_calls == 0);

	reset_fixture(&parameters);
	establish_error = ENOMEM;
	CHECK("process-window admission error is returned",
	    exec_elf_check(&parameters) == ENOMEM);
	CHECK("process-window admission error skips segment read",
	    establish_calls == 1 && segment_reads == 0 && signal_calls == 0);
	CHECK("process-window admission error withholds publication",
	    clear_calls == 0 && setup_calls == 0);
}

int
main(void)
{
	(void)&psignal;
	check_valid_segment();
	check_zero_bss_segment();
	check_bad_header_geometry();
	check_bad_memory_geometry();
	check_bad_segment_shape();
	check_bad_file_geometry();
	check_bad_entry();
	check_read_failures();
	check_precommit_failures();
	return failures != 0;
}
