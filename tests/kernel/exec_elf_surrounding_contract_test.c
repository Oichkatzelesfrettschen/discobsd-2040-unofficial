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
#ifndef EXEC_ESTAB_FUNCTION
#define EXEC_ESTAB_FUNCTION "exec_estab_function.c"
#endif
#ifndef EXEC_SETUPSTACK_FUNCTION
#define EXEC_SETUPSTACK_FUNCTION "exec_setupstack_function.c"
#endif
#ifndef SWAPTEXT_FUNCTION
#define SWAPTEXT_FUNCTION "swaptext_function.c"
#endif
#ifndef EXEC_TEXT_RELEASE_FUNCTION
#define EXEC_TEXT_RELEASE_FUNCTION "exec_text_release_function.c"
#endif

typedef char *caddr_t;
typedef unsigned int u_int;
typedef unsigned short u_short;

#define DEBUG(...) ((void)0)
#define ELF32_MACHDEP_ID EM_ARM
#define ENOEXEC 8
#define ENOMEM 12
#define EIO 5
#define IO_UNIT 0
#define UIO_READ 0
#define SSIZE 2048U
#define NBPW 4U
#define USER_WINDOW (144U * 1024U)
#define USER_WINDOW_MAX USER_WINDOW
#define MAXMEM USER_WINDOW
#define STACKALIGN 8U
#define SVFORK 0x1000
#define EXEC_SPOOL_BLOCK_NONE 0xffff
#define EXEC_COMMITTED_FAILURE 0x02
#define MAXCOMLEN 16
#define NO_ADDR ((caddr_t)(uintptr_t)UINT_MAX)
#define USER_BASE ((uintptr_t)user_window)
#define __user_data_start ((caddr_t)USER_BASE)
#define USER_TOP(process_pointer) (USER_BASE + USER_WINDOW)
#define roundup(value, alignment) \
	(((value) + (alignment) - 1U) & ~((alignment) - 1U))
#define __thumb__ 1

static _Alignas(8) unsigned char user_window[USER_WINDOW];

struct inode;

struct proc {
	int p_pid;
	int p_flag;
	size_t p_dsize;
	size_t p_daddr;
	size_t p_ssize;
	size_t p_saddr;
	size_t p_tsize;
	struct inode *p_tip;
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
	unsigned char flags;
	struct inode *ip;
	struct memsect text;
	struct memsect data;
	struct memsect bss;
	struct memsect heap;
	struct memsect stack;
	struct {
		unsigned int size;
		unsigned int pos;
		unsigned short readblock;
	} spool;
};

struct trapframe {
	u_int tf_r0;
	u_int tf_r1;
	u_int tf_r2;
	u_int tf_r3;
	u_int tf_ip;
	u_int tf_lr;
	u_int tf_pc;
	u_int tf_psr;
	u_int tf_r4;
	u_int tf_r5;
	u_int tf_r6;
	u_int tf_r7;
	u_int tf_r8;
	u_int tf_r9;
	u_int tf_r10;
	u_int tf_r11;
};
#define tf_sp tf_ip

struct user {
	struct proc *u_procp;
	struct trapframe *u_frame;
	struct {
		unsigned int pr_scale;
	} u_prof;
	unsigned int u_rval;
	char u_comm[MAXCOMLEN + 1];
};

static struct user u;
static struct proc process;
static struct trapframe trapframe;
static struct inode executable_inode;
static struct inode prior_executable;
static struct elf_phdr supplied_program_header;
static unsigned char program_header_storage[sizeof(struct elf_phdr)];
static unsigned int spool_part;
static struct inode *released_inode;
static unsigned int inode_lock_calls;
static unsigned int inode_release_calls;
static unsigned int text_unhold_calls;
static unsigned int direct_release_calls;
static unsigned int failures;

static void *
exec_alloc(int size, int alignment, struct exec_params *parameters)
{
	(void)alignment;
	(void)parameters;
	if (size != (int)sizeof(program_header_storage))
		return NULL;
	return program_header_storage;
}

static int
rdwri(int operation, struct inode *inode, caddr_t destination, int length,
    off_t offset, int flags, int *residual)
{
	(void)operation;
	(void)inode;
	(void)offset;
	(void)flags;
	if (residual != NULL)
		*residual = 0;
	if (destination == (caddr_t)program_header_storage) {
		memcpy(destination, &supplied_program_header,
		    sizeof(supplied_program_header));
		return 0;
	}
	if (length < 0 || destination < (caddr_t)user_window ||
	    destination + length > (caddr_t)user_window + sizeof(user_window))
		return ENOEXEC;
	return 0;
}

static int
exec_save_args(struct exec_params *parameters)
{
	parameters->argc = 200;
	parameters->envc = 0;
	parameters->argbc = 200;
	parameters->envbc = 0;
	parameters->spool.size = 3U * parameters->argc;
	return 0;
}

static void
endvfork(void)
{
}

static void
exec_clear(struct exec_params *parameters)
{
	(void)parameters;
}

static void
exec_spool_read(struct exec_params *parameters, void *destination,
    unsigned int length)
{
	if ((spool_part & 1U) == 0) {
		u_short record_length = 1;

		memcpy(destination, &record_length, length);
	} else {
		memset(destination, 0, length);
	}
	spool_part++;
	parameters->spool.pos += length;
}

static void
panic(const char *message)
{
	fprintf(stderr, "PANIC %s\n", message);
	failures++;
}

static int
copystr(const char *source, char *destination, unsigned int length, void *done)
{
	(void)source;
	(void)destination;
	(void)length;
	(void)done;
	return 0;
}

static void
irele(struct inode *inode)
{
	(void)inode;
	direct_release_calls++;
}

static void
ilock(struct inode *inode)
{
	(void)inode;
	inode_lock_calls++;
}

static void
exec_text_unhold(struct inode *inode)
{
	released_inode = inode;
	text_unhold_calls++;
}

static void
iput(struct inode *inode)
{
	(void)inode;
	inode_release_calls++;
}

#include EXEC_ESTAB_FUNCTION
#include EXEC_SETUPSTACK_FUNCTION
#include EXEC_TEXT_RELEASE_FUNCTION
#include EXEC_ELF_FUNCTION
#include SWAPTEXT_FUNCTION

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
	unsigned int stack_length;

	memset(parameters, 0, sizeof(*parameters));
	memset(&process, 0, sizeof(process));
	memset(&trapframe, 0, sizeof(trapframe));
	memset(user_window, 0xa5, sizeof(user_window));
	memset(program_header_storage, 0, sizeof(program_header_storage));
	memset(&supplied_program_header, 0, sizeof(supplied_program_header));
	memcpy(parameters->hdr.elf.e_ident, elf_ident, sizeof(elf_ident));
	parameters->hdr.elf.e_type = ET_EXEC;
	parameters->hdr.elf.e_machine = EM_ARM;
	parameters->hdr.elf.e_version = EV_CURRENT;
	parameters->hdr.elf.e_entry = (unsigned int)USER_BASE + 1U;
	parameters->hdr.elf.e_phoff = sizeof(struct elf_ehdr);
	parameters->hdr.elf.e_ehsize = sizeof(struct elf_ehdr);
	parameters->hdr.elf.e_phentsize = sizeof(struct elf_phdr);
	parameters->hdr.elf.e_phnum = 1;
	parameters->hdr.elf.e_shentsize = sizeof(struct elf_shdr);
	parameters->hdr.elf.e_shnum = 1;
	parameters->hdr_len = sizeof(struct elf_ehdr);
	parameters->ip = &executable_inode;
	stack_length = SSIZE + 200U + (200U + 4U) * NBPW;
	supplied_program_header.p_type = PT_LOAD;
	supplied_program_header.p_offset = 0x100;
	supplied_program_header.p_vaddr = (unsigned int)USER_BASE;
	supplied_program_header.p_paddr = (unsigned int)USER_BASE;
	supplied_program_header.p_memsz = USER_WINDOW - stack_length;
	supplied_program_header.p_filesz = supplied_program_header.p_memsz;
	supplied_program_header.p_flags = PF_R | PF_W | PF_X;
	supplied_program_header.p_align = NBPW;
	executable_inode.i_size = (off_t)(supplied_program_header.p_offset +
	    supplied_program_header.p_filesz);
	process.p_pid = 2;
	process.p_tip = &prior_executable;
	process.p_tsize = 64;
	u.u_procp = &process;
	u.u_frame = &trapframe;
	spool_part = 0;
	released_inode = NULL;
	inode_lock_calls = 0;
	inode_release_calls = 0;
	text_unhold_calls = 0;
	direct_release_calls = 0;
}

static void
check_stack_publication_stays_in_admitted_extent(void)
{
	struct exec_params parameters;
	uintptr_t stack_base;
	uintptr_t argument_vector;
	uintptr_t stack_pointer;
	unsigned int index;
	unsigned int changed_below_stack = 0;

	reset_fixture(&parameters);
	CHECK("boundary ELF succeeds", exec_elf_check(&parameters) == 0);
	stack_base = (uintptr_t)parameters.stack.vaddr;
	argument_vector = (uintptr_t)trapframe.tf_r1;
	stack_pointer = (uintptr_t)trapframe.tf_sp;
	for (index = 0; index < stack_base - USER_BASE; index++) {
		if (user_window[index] != 0xa5)
			changed_below_stack++;
	}
	CHECK("argument vector stays inside admitted stack",
	    argument_vector >= stack_base);
	CHECK("published stack pointer stays inside admitted stack",
	    stack_pointer >= stack_base);
	CHECK("stack publication preserves admitted image bytes",
	    changed_below_stack == 0);
}

static void
check_elf_replacement_clears_file_backed_text(void)
{
	struct exec_params parameters;

	reset_fixture(&parameters);
	CHECK("replacement ELF succeeds", exec_elf_check(&parameters) == 0);
	CHECK("replacement releases preceding text owner",
	    process.p_tip == NULL && released_inode == &prior_executable &&
	    inode_lock_calls == 1 && text_unhold_calls == 1 &&
	    inode_release_calls == 1 && direct_release_calls == 0);
	CHECK("replacement publishes zero file-backed text length",
	    process.p_tsize == 0);
	CHECK("swap restoration selects the complete ELF image",
	    swaptext(&process) == 0);
}

int
main(void)
{
	check_stack_publication_stays_in_admitted_extent();
	check_elf_replacement_clears_file_backed_text();
	return failures != 0;
}
