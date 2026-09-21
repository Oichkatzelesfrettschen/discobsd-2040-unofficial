TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc

PORT_INCLUDE=	${TOPSRC}/sys/arch/${MACHINE}/include

# sys/param.h reaches a port's headers as <machine/*.h>. config(8) spells that
# name as a symlink in the kernel build directory, which a gate that has to
# run in an unconfigured tree cannot rely on; this tier generates one
# forwarding header per port header instead, so it needs neither a cross build
# nor a name outside the tree.
MACHINE_STAMP=	machine/.stamp

# Kernel sources reach libc's namespace when they link into a host binary.
# printf() would take over every libc call to that name, and sys/kern's
# malloc() and mfree() take a resource map where libc's take a byte count, so
# the four names move aside. hostkern_kern.c compiles with this same set and
# includes both the harness header and the kernel's, which is what holds the
# harness to the signatures the kernel declares.
KERNEL_RENAMES=	-Dprintf=hk_kprintf \
		-Dmalloc=rmap_malloc -Dmfree=rmap_mfree \
		-Dmalloc3=rmap_malloc3 \
		-Dmalloc3_contiguous=rmap_malloc3_contiguous \
		-Dmalloc3_contiguous_next=rmap_malloc3_contiguous_next

# -ffreestanding and -fno-builtin are what sys/arch/${MACHINE}/conf builds the
# kernel with, and they are also what keeps the compiler from holding
# sys/systm.h's declarations of printf, log and strncmp against its own.
# clang checks the operand widths of an asm statement it never emits, so the
# port's PRIMASK inlines have to be excused even though hostintr.h redefines
# every macro that would call them. tests/rp2040/exec_spool and cache_footprint
# excuse the same header the same way.
KERN_COMMON=	-std=gnu17 -ffreestanding -fno-builtin \
		-Wno-asm-operand-widths \
		-DKERNEL -I. -I${TOPSRC}/sys -I${TOPSRC}/sys/arch
KERN_BASE=	${KERN_COMMON} ${KERNEL_RENAMES}

# A kernel source is held to the warning set the kernel is built with, and no
# more: a gate that rejected code the production build accepts would fail for
# something that ships. The gate's own sources take the stricter set.
#
# sys/kern still defines functions the old way, and clang rejects that by
# default while the arm-none-eabi gcc the kernel is built with does not. The
# gate excuses it rather than rewriting kernel sources a host compiler is the
# only thing to complain about; bin/sh/tests and usr.bin/sort's runner excuse
# it for the same reason.
KERN_WARN=	-Wall -Wextra -Werror -Wno-deprecated-non-prototype
GATE_WARN=	-Wall -Wextra -Werror

KERN_CFLAGS=	${KERN_BASE} ${KERN_WARN}
GATE_CFLAGS=	${KERN_BASE} ${GATE_WARN}
HARNESS_CFLAGS=	-std=gnu17 ${GATE_WARN}

# sys/kern/subr_prf.c walks its own arguments as an array of four-byte slots,
# so it is faithful only where a pointer, a long and an int are four bytes
# and arguments sit on the stack. That is the target, and it is what -m32
# gives on an x86-64 host: an ordinary 32-bit userspace binary, no virtual
# machine and no second operating system. A host without 32-bit support
# builds the rest of the tier and says so.
ILP32=		-m32
ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/ilp32probe$$$$.c; \
		if ${HOST_CC} -m32 -o /dev/null /tmp/ilp32probe$$$$.c 2>/dev/null; \
		then echo yes; else echo no; fi; \
		rm -f /tmp/ilp32probe$$$$.c

# printf and panic are both defined by subr_prf.c and both owned by libc in a
# host binary, so they move aside the way malloc does elsewhere in this tier.
#
# subr_prf.c defines printf and panic itself, so this gate cannot carry the
# tier's printf rename as well; it takes KERN_COMMON with its own pair.
PRF_RENAMES=	-Dprintf=kern_printf -Dpanic=kern_panic
PRF_KERN_CFLAGS=	${KERN_COMMON} ${PRF_RENAMES} ${KERN_WARN} ${ILP32}
PRF_GATE_CFLAGS=	${KERN_COMMON} ${PRF_RENAMES} ${GATE_WARN} ${ILP32}
PRF_SRC=	${TOPSRC}/sys/kern/subr_prf.c

# The sysctl(2) tree. vm_sysctl()'s extents are pointer differences held in
# an int, and fill_from_u() reads a process u area through an int-valued
# p_addr, so kern_sysctl.c compiles at the target's width alone.
SYSCTL_SRC=	${TOPSRC}/sys/kern/kern_sysctl.c

# The inode allocator. struct dinode is an on-disk layout and INOPB is
# MAXBSIZE divided by its size, so one block holds exactly INOPB inodes only
# where off_t and time_t are four bytes; ufs_alloc.c compiles at the target's
# width alone.
IALLOC_SRC=	${TOPSRC}/sys/kern/ufs_alloc.c

# sys/arch/rp2040/compile/PICO reaches the superblock through mount[0] and
# packs the in-core inode's flag words, so the gate runs in that shape beside
# the portable one, as rmap_test runs in both descriptor shapes. A DIAGNOSTIC
# kernel adds ialloc()'s itoo() assertion on fs_lasti.
IALLOC_PICO_DEFS=	-DSINGLE_UFS_ROOT -DNMOUNT=1 -DLINEAR_INODE_CACHE \
			-DCOMPACT_INODE_FIELDS -DLINEAR_BUFFER_CACHE
IALLOC_DIAG_DEFS=	-DDIAGNOSTIC

# The block allocator, from the same translation unit. struct fs is an
# on-disk layout that fills exactly one DEV_BSIZE block, so balloc()'s
# "*fps = *fs" writes one block only at the target's width, as ialloc_test
# needs it for struct dinode.
#
# ufs_alloc.c's free() takes an inode and a block number where libc's takes a
# pointer, and a definition in the executable interposes on every libc call to
# that name, so it moves aside the way malloc() and mfree() do above. Both the
# kernel object and the gate compile with the rename, which is what keeps the
# gate calling the allocator's free() and libc calling its own.
BALLOC_SRC=	${TOPSRC}/sys/kern/ufs_alloc.c
BALLOC_RENAMES=	-Dfree=ufs_free
BALLOC_PICO_DEFS=	${IALLOC_PICO_DEFS}

# kill() and the cansignal() it decides with, over seteuid() and the
# process lookups. kern_sig.c casts pointers to int in issignal() and
# core(), so it compiles at the target's width alone, beside subr_prf.c.
SIGAUTH_SRC=	${TOPSRC}/sys/kern/kern_sig.c ${TOPSRC}/sys/kern/kern_prot2.c \
		${TOPSRC}/sys/kern/kern_proc.c

RMAP_SRC=	${TOPSRC}/sys/kern/subr_rmap.c
UIO_SRC=	${TOPSRC}/sys/kern/kern_subr.c
CLIST_SRC=	${TOPSRC}/sys/kern/tty_subr.c

# The protection syscalls and the process lookups they decide with.
CRED_SRC=	${TOPSRC}/sys/kern/kern_prot.c ${TOPSRC}/sys/kern/kern_prot2.c \
		${TOPSRC}/sys/kern/kern_proc.c

# The resource map allocator runs in three shapes across the maintained ports:
# the sixteen-bit descriptors sys/arch/rp2040/compile/PICO configures, the
# wide descriptors STM32 uses, and a DIAGNOSTIC kernel, where
# mfree()'s three overlap panics compile in.
COMPACT_DEFS=		-DCOMPACT_SWAPMAP
WIDE_DEFS=
DIAGNOSTIC_DEFS=	-DCOMPACT_SWAPMAP -DDIAGNOSTIC

PROGS=	rmap_test_compact rmap_test_wide rmap_test_diagnostic uio_test \
	clist_test cred_test resource_test rwuio_test

# The read, write, readv and writev entry points. rwuio() calls the port's
# setjmp on u_qsave and sleep() longjmps back to it, so rwuio_setjmp.h maps
# the call onto the host's setjmp over a jmp_buf rwuio_jump.c owns, and the
# gate's file operation stub answers through the host's longjmp.
RWUIO_SRC=	${TOPSRC}/sys/kern/sys_generic.c
RWUIO_DEFS=	-include rwuio_setjmp.h
RWUIO_JUMP=	rwuio_jump.c rwuio_jump.h

all: ${PROGS}

${MACHINE_STAMP}: hostintr.h
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/include/$$name>" > machine/$$name; \
	done
	@echo '#include "../hostintr.h"' >> machine/intr.h
	@touch $@

# The harness carries no kernel header, so it compiles without the renames
# and reaches <setjmp.h>, <stdio.h> and <string.h> the way any host program
# does. hostkern_kern.c is the opposite case and builds once per shape,
# because the kernel's own headers decide its types.
hostkern.o: hostkern.c hostkern.h
	${HOST_CC} ${HARNESS_CFLAGS} -c -o $@ hostkern.c

rmap_test_compact: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${COMPACT_DEFS} -c -o rmap_kernel_compact.o \
	    ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${COMPACT_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_compact.o hostkern.o

rmap_test_wide: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${WIDE_DEFS} -c -o rmap_kernel_wide.o \
	    ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${WIDE_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_wide.o hostkern.o

rmap_test_diagnostic: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${DIAGNOSTIC_DEFS} \
	    -c -o rmap_kernel_diagnostic.o ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${DIAGNOSTIC_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_diagnostic.o hostkern.o

uio_test: uio_test.c ${UIO_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o uio_kernel.o ${UIO_SRC}
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    uio_test.c hostkern_kern.c uio_kernel.o hostkern.o

clist_test: clist_test.c ${CLIST_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o clist_kernel.o ${CLIST_SRC}
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    clist_test.c hostkern_kern.c clist_kernel.o hostkern.o

cred_test: cred_test.c ${CRED_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o cred_prot.o ${TOPSRC}/sys/kern/kern_prot.c
	${HOST_CC} ${KERN_CFLAGS} -c -o cred_prot2.o ${TOPSRC}/sys/kern/kern_prot2.c
	${HOST_CC} ${KERN_CFLAGS} -c -o cred_proc.o ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ cred_test.c hostkern_kern.c \
	    cred_prot.o cred_prot2.o cred_proc.o hostkern.o

resource_test: resource_test.c ${TOPSRC}/sys/kern/kern_resource.c \
    ${TOPSRC}/sys/kern/kern_proc.c hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o resource_kernel.o \
	    ${TOPSRC}/sys/kern/kern_resource.c
	${HOST_CC} ${KERN_CFLAGS} -c -o resource_proc.o \
	    ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ resource_test.c hostkern_kern.c \
	    resource_kernel.o resource_proc.o hostkern.o

rwuio_test: rwuio_test.c rwuio_setjmp.h ${RWUIO_JUMP} ${RWUIO_SRC} \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${RWUIO_DEFS} -c -o rwuio_kernel.o ${RWUIO_SRC}
	${HOST_CC} ${HARNESS_CFLAGS} -c -o rwuio_jump.o rwuio_jump.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    rwuio_test.c hostkern_kern.c rwuio_kernel.o rwuio_jump.o hostkern.o

# The harness again at the narrower width, since every object in one binary
# has to agree.
hostkern32.o: hostkern.c hostkern.h
	${HOST_CC} ${HARNESS_CFLAGS} ${ILP32} -c -o $@ hostkern.c

prf_test: prf_test.c ${PRF_SRC} hostkern_kern.c hostkern32.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${PRF_KERN_CFLAGS} -c -o prf_kernel.o ${PRF_SRC}
	${HOST_CC} ${PRF_KERN_CFLAGS} -c -o prf_shim.o hostkern_kern.c
	${HOST_CC} ${PRF_GATE_CFLAGS} -o $@ \
	    prf_test.c prf_kernel.o prf_shim.o hostkern32.o

sigauth_test: sigauth_test.c ${SIGAUTH_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_sig.o \
	    ${TOPSRC}/sys/kern/kern_sig.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_prot2.o \
	    ${TOPSRC}/sys/kern/kern_prot2.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_proc.o \
	    ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -o $@ sigauth_test.c \
	    sigauth_sig.o sigauth_prot2.o sigauth_proc.o sigauth_shim.o hostkern32.o

# The node's extent is a count of mapents, so the gate runs in both
# descriptor shapes: the sixteen-bit entries PICO configures and the wide
# entries STM32 uses, as rmap_test does.
sysctl_test_compact: sysctl_test.c ${SYSCTL_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${COMPACT_DEFS} ${ILP32} \
	    -c -o sysctl_kernel_compact.o ${SYSCTL_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sysctl_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${COMPACT_DEFS} ${ILP32} -o $@ sysctl_test.c \
	    sysctl_kernel_compact.o sysctl_shim.o hostkern32.o

sysctl_test_wide: sysctl_test.c ${SYSCTL_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${WIDE_DEFS} ${ILP32} \
	    -c -o sysctl_kernel_wide.o ${SYSCTL_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sysctl_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${WIDE_DEFS} ${ILP32} -o $@ sysctl_test.c \
	    sysctl_kernel_wide.o sysctl_shim.o hostkern32.o

# The inode allocator in the three shapes the tree configures it in: the
# portable one, sys/arch/rp2040/compile/PICO's, and a DIAGNOSTIC kernel.
ialloc_test: ialloc_test.c ${IALLOC_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o ialloc_kernel.o ${IALLOC_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o ialloc_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -o $@ ialloc_test.c \
	    ialloc_kernel.o ialloc_shim.o hostkern32.o

ialloc_test_pico: ialloc_test.c ${IALLOC_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${IALLOC_PICO_DEFS} ${ILP32} \
	    -c -o ialloc_kernel_pico.o ${IALLOC_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${IALLOC_PICO_DEFS} ${ILP32} \
	    -c -o ialloc_shim_pico.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${IALLOC_PICO_DEFS} ${ILP32} -o $@ \
	    ialloc_test.c ialloc_kernel_pico.o ialloc_shim_pico.o hostkern32.o

ialloc_test_diagnostic: ialloc_test.c ${IALLOC_SRC} hostkern_kern.c \
    hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${IALLOC_DIAG_DEFS} ${ILP32} \
	    -c -o ialloc_kernel_diagnostic.o ${IALLOC_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o ialloc_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${IALLOC_DIAG_DEFS} ${ILP32} -o $@ \
	    ialloc_test.c ialloc_kernel_diagnostic.o ialloc_shim.o hostkern32.o

# The block allocator in the portable shape and in PICO's, which reaches the
# superblock through mount[0].
balloc_test: balloc_test.c ${BALLOC_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${BALLOC_RENAMES} ${ILP32} \
	    -c -o balloc_kernel.o ${BALLOC_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o balloc_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${BALLOC_RENAMES} ${ILP32} -o $@ \
	    balloc_test.c balloc_kernel.o balloc_shim.o hostkern32.o

balloc_test_pico: balloc_test.c ${BALLOC_SRC} hostkern_kern.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${BALLOC_PICO_DEFS} ${BALLOC_RENAMES} \
	    ${ILP32} -c -o balloc_kernel_pico.o ${BALLOC_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${BALLOC_PICO_DEFS} ${ILP32} \
	    -c -o balloc_shim_pico.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${BALLOC_PICO_DEFS} ${BALLOC_RENAMES} \
	    ${ILP32} -o $@ balloc_test.c balloc_kernel_pico.o \
	    balloc_shim_pico.o hostkern32.o

# rwuio's bound is a 32-bit fact: off_t, size_t and u_int are all four
# bytes on the target, so the sum that wrapped is reproduced only here.
rwuio_test32: rwuio_test.c rwuio_setjmp.h ${RWUIO_JUMP} ${RWUIO_SRC} \
    hostkern_kern.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${RWUIO_DEFS} ${ILP32} -c -o rwuio_kernel32.o \
	    ${RWUIO_SRC}
	${HOST_CC} ${HARNESS_CFLAGS} ${ILP32} -c -o rwuio_jump32.o rwuio_jump.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o rwuio_shim32.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -DHK_ILP32 -o $@ \
	    rwuio_test.c rwuio_kernel32.o rwuio_shim32.o rwuio_jump32.o hostkern32.o

# The narrow gate, which the root Makefile reaches as check-kernel-ilp32.
check-ilp32:
.if ${ILP32_OK} == "yes"
	@${MAKE} prf_test rwuio_test32 sigauth_test \
	    sysctl_test_compact sysctl_test_wide \
	    ialloc_test ialloc_test_pico ialloc_test_diagnostic \
	    balloc_test balloc_test_pico
	./prf_test
	./sigauth_test
	./rwuio_test32
	./sysctl_test_compact
	./sysctl_test_wide
	./ialloc_test
	./ialloc_test_pico
	./ialloc_test_diagnostic
	./balloc_test
	./balloc_test_pico
.else
	@echo "check-kernel-ilp32: ${HOST_CC} cannot build 32-bit binaries; skipped" >&2
.endif

check: ${PROGS}
	@for prog in ${PROGS}; do \
		echo "run ./$$prog"; \
		./$$prog || exit 1; \
	done

clean:
	rm -f ${PROGS} hostkern.o
	rm -f rmap_kernel_compact.o rmap_kernel_wide.o rmap_kernel_diagnostic.o
	rm -f uio_kernel.o clist_kernel.o
	rm -f cred_prot.o cred_prot2.o cred_proc.o
	rm -f resource_kernel.o resource_proc.o
	rm -f prf_kernel.o prf_shim.o hostkern32.o prf_test
	rm -f sigauth_sig.o sigauth_prot2.o sigauth_proc.o sigauth_shim.o sigauth_test
	rm -f rwuio_kernel.o rwuio_kernel32.o rwuio_shim32.o rwuio_test32
	rm -f rwuio_jump.o rwuio_jump32.o
	rm -f sysctl_kernel_compact.o sysctl_kernel_wide.o sysctl_shim.o
	rm -f sysctl_test_compact sysctl_test_wide
	rm -f ialloc_kernel.o ialloc_kernel_pico.o ialloc_kernel_diagnostic.o
	rm -f ialloc_shim.o ialloc_shim_pico.o
	rm -f ialloc_test ialloc_test_pico ialloc_test_diagnostic
	rm -f balloc_kernel.o balloc_kernel_pico.o
	rm -f balloc_shim.o balloc_shim_pico.o
	rm -f balloc_test balloc_test_pico
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

.PHONY: all check check-ilp32 clean
