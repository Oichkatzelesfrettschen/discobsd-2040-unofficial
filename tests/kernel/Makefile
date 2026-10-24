TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc

PORT_INCLUDE=	${TOPSRC}/sys/arch/${MACHINE}/include
USERCOPY_HEADER=	${PORT_INCLUDE}/usercopy.h

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
SYSCALL_GATE_CFLAGS=	-std=c17 -pedantic-errors ${GATE_WARN}
CRED_KERN_CFLAGS=	${KERN_CFLAGS} -Wstrict-prototypes \
			-Wold-style-definition
KERNEL_SANITIZER_FLAGS=	-fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-omit-frame-pointer
KERNEL_SANITIZER_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh native \
			${HOST_CC} ${KERNEL_SANITIZER_FLAGS}

# The surrounding ELF fixture includes the exact production swaptext()
# definition.  That inherited definition uses K&R parameter syntax, so Clang
# needs the same narrow exception as other exact kernel-source gates.
ELF_SURROUNDING_CFLAGS=	${SYSCALL_GATE_CFLAGS} \
			-Wno-deprecated-non-prototype

# sys/kern/subr_prf.c walks its own arguments as an array of four-byte slots,
# so it is faithful only where a pointer, a long and an int are four bytes
# and arguments sit on the stack. That is the target, and it is what -m32
# gives on an x86-64 host: an ordinary 32-bit userspace binary, no virtual
# machine and no second operating system. A host without 32-bit support
# builds the rest of the tier and says so.
ILP32=		-m32
ILP32_CC= ${HOST_CC}
include ${TOPSRC}/tools/test-execution.mk

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

# ufs_setattr() carries explicit utimes(2) through MNT_NOATIME. Function
# sections let the gate link that routine from ufs_fio.c without inventing
# stubs for the unrelated access and mount helpers in the same source file.
UFS_EXPLICIT_TIME_SRC=	${TOPSRC}/sys/kern/ufs_fio.c
UFS_EXPLICIT_TIME_CRED_SRC=	${TOPSRC}/sys/kern/kern_prot2.c
UFS_EXPLICIT_TIME_DEFS=	${IALLOC_PICO_DEFS}
UFS_EXPLICIT_TIME_CFLAGS=	-ffunction-sections
UFS_EXPLICIT_TIME_LDFLAGS=	-Wl,--gc-sections

# The RP2040 shutdown path. The production helper uses bounded flags, counts
# and kernel structure fields, so the host-width gate can exercise its
# sequencing and output; both production configurations compile it separately
# for Cortex-M0+.
SHUTDOWN_SYNC_SRC=	${TOPSRC}/sys/arch/rp2040/rp2040/shutdown_sync.c
MACHDEP_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/machdep.c
USERCOPY_SRC=		${MACHDEP_SRC}
SYSCALL_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/syscall.c
SIGRETURN_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/sig_machdep.c
EXEC_ELF_SRC=		${TOPSRC}/sys/kern/exec_elf.c
KERN_EXEC_SRC=		${TOPSRC}/sys/kern/kern_exec.c
EXEC_SUBR_SRC=		${TOPSRC}/sys/kern/exec_subr.c
VM_SWAP_SRC=		${TOPSRC}/sys/kern/vm_swap.c
EXEC_AOUT_SRC=		${TOPSRC}/sys/kern/exec_aout.c

# kill() and the cansignal() it decides with, over seteuid() and the
# process lookups. kern_sig.c casts pointers to int in issignal() and
# core(), so it compiles at the target's width alone, beside subr_prf.c.
SIGAUTH_SRC=	${TOPSRC}/sys/kern/kern_sig.c ${TOPSRC}/sys/kern/kern_prot2.c \
		${TOPSRC}/sys/kern/kern_proc.c

RMAP_SRC=	${TOPSRC}/sys/kern/subr_rmap.c
SWAP_IOCTL_SRC=	${TOPSRC}/sys/dev/swap.c
SWAP_IOCTL_FUNCTION=	swap_ioctl_function.c
SWAP_IOCTL_DEFS=	${COMPACT_DEFS}
SWAP_IOCTL_C17_CFLAGS=	${GATE_CFLAGS} -std=c17 -pedantic-errors \
			-Wstrict-prototypes -Wold-style-definition
SWAP_IOCTL_MUTANTS=	release-fallthrough media-size-fallthrough \
	shortage-output-suppression unrounded-free stale-released-size
SWAP_IOCTL_HEADER_DEPS=	hostkern.h hostintr.h \
	${TOPSRC}/sys/sys/buf.h ${TOPSRC}/sys/sys/cdefs.h \
	${TOPSRC}/sys/sys/conf.h ${TOPSRC}/sys/sys/disk.h \
	${TOPSRC}/sys/sys/errno.h ${TOPSRC}/sys/sys/fcntl.h \
	${TOPSRC}/sys/sys/ioctl.h ${TOPSRC}/sys/sys/map.h \
	${TOPSRC}/sys/sys/param.h ${TOPSRC}/sys/sys/swap.h \
	${TOPSRC}/sys/sys/select.h ${TOPSRC}/sys/sys/signal.h \
	${TOPSRC}/sys/sys/stdint.h ${TOPSRC}/sys/sys/syscall_dispatch.h \
	${TOPSRC}/sys/sys/syslimits.h ${TOPSRC}/sys/sys/systm.h \
	${TOPSRC}/sys/sys/ttychars.h ${TOPSRC}/sys/sys/ttydev.h \
	${TOPSRC}/sys/sys/types.h ${TOPSRC}/sys/sys/vm.h \
	${TOPSRC}/sys/sys/vmparam.h ${TOPSRC}/sys/sys/vmmac.h \
	${TOPSRC}/sys/sys/vmmeter.h ${TOPSRC}/sys/sys/vmsystm.h \
	${PORT_INCLUDE}/intr.h ${PORT_INCLUDE}/limits.h \
	${PORT_INCLUDE}/machparam.h ${PORT_INCLUDE}/scb.h \
	${PORT_INCLUDE}/signal.h ${PORT_INCLUDE}/types.h ${MACHINE_STAMP}
SWAP_IOCTL_BUILD_TARGETS=	swap_ioctl_function.o swap_ioctl_rmap.o \
	swap_ioctl_test swap_ioctl_function32.o swap_ioctl_rmap32.o \
	swap_ioctl_test32 swap_ioctl_test_sanitized \
	${SWAP_IOCTL_MUTANTS:@mutation@swap_ioctl_${mutation}.mutant.test@}
UIO_SRC=	${TOPSRC}/sys/kern/kern_subr.c
CLIST_SRC=	${TOPSRC}/sys/kern/tty_subr.c
CAPACITY_SRC=	${TOPSRC}/sys/kern/subr_capacity.c
CAPACITY_DEFS=	-DCAPACITY_STATS -DSWAPRAM ${IALLOC_PICO_DEFS}
CAPACITY_MUTANTS=	process-state inode-references clist-object-size \
		buffer-any-flag peak-follows-down saturation-inverted

# The protection syscalls and the process lookups they decide with.
CRED_SRC=	${TOPSRC}/sys/kern/kern_prot.c ${TOPSRC}/sys/kern/kern_prot2.c \
		${TOPSRC}/sys/kern/kern_proc.c

# File-owner conversion and the fcntl/ioctl entry points that publish it.
# Function sections retain only the caller-complete unit from the two kernel
# translation units and avoid linking unrelated descriptor/syscall services.
FILE_OWNER_DESCRIP_SRC=	${TOPSRC}/sys/kern/kern_descrip.c
FILE_OWNER_GENERIC_SRC=	${TOPSRC}/sys/kern/sys_generic.c
FILE_OWNER_CFLAGS=	-ffunction-sections -fdata-sections
.if ${_HOST_OSNAME} == "Darwin"
FILE_OWNER_LDFLAGS=	-Wl,-dead_strip
.else
FILE_OWNER_LDFLAGS=	-Wl,--gc-sections
.endif
FILE_OWNER_DESCRIP_MUTANTS=	unconditional-publication
FILE_OWNER_GENERIC_MUTANTS=	direct-user-pointer
FILE_OWNER_UBSAN_MUTANTS=	unchecked-negation
FILE_OWNER_NATIVE_MUTANTS=	${FILE_OWNER_DESCRIP_MUTANTS} \
	${FILE_OWNER_GENERIC_MUTANTS}
FILE_STATUS_DESCRIP_MUTANTS=	early-fset-publication \
		early-setfl-publication zero-setfl-rollback
FILE_STATUS_GENERIC_MUTANTS=	direct-status-pointer
FILE_STATUS_MUTANTS=	${FILE_STATUS_DESCRIP_MUTANTS} \
	${FILE_STATUS_GENERIC_MUTANTS}
FIONREAD_GENERIC_SRC=	${TOPSRC}/sys/kern/sys_generic.c
FIONREAD_INODE_SRC=	${TOPSRC}/sys/kern/sys_inode.c
FIONREAD_IOCTL_FUNCTION=	fionread_ioctl_function.c
FIONREAD_INODE_FUNCTION=	fionread_inode_function.c
FIONREAD_CFLAGS=	-ffunction-sections -fdata-sections
FIONREAD_C17_CFLAGS=	${GATE_CFLAGS} -std=c17 \
	-Wstrict-prototypes -Wold-style-definition
FIONREAD_LDFLAGS=	${FILE_OWNER_LDFLAGS}
FIONREAD_GENERIC_RENAMES=	-Dioctl=fionread_ioctl
FIONREAD_INODE_RENAMES=	-Dsetjmp=fionread_setjmp
FIONREAD_MUTANTS=	direct-user-pointer unconditional-publication wrong-width \
			unmasked-size endpoint-span direct-input-pointer \
			short-input-copy masked-input-command \
			unconditional-inout-publication omitted-inout-publication
FIONREAD_HEADER_DEPS=	hostkern.h hostintr.h \
	${TOPSRC}/sys/sys/buf.h ${TOPSRC}/sys/sys/capacity.h \
	${TOPSRC}/sys/sys/cdefs.h ${TOPSRC}/sys/sys/conf.h \
	${TOPSRC}/sys/sys/dir.h ${TOPSRC}/sys/sys/errno.h \
	${TOPSRC}/sys/sys/exec.h ${TOPSRC}/sys/sys/exec_aout.h \
	${TOPSRC}/sys/sys/exec_elf.h ${TOPSRC}/sys/sys/fcntl.h \
	${TOPSRC}/sys/sys/file.h ${TOPSRC}/sys/sys/fs.h \
	${TOPSRC}/sys/sys/inode.h ${TOPSRC}/sys/sys/ioctl.h \
	${TOPSRC}/sys/sys/kernel.h ${TOPSRC}/sys/sys/mount.h \
	${TOPSRC}/sys/sys/param.h ${TOPSRC}/sys/sys/proc.h \
	${TOPSRC}/sys/sys/resource.h ${TOPSRC}/sys/sys/select.h \
	${TOPSRC}/sys/sys/signal.h ${TOPSRC}/sys/sys/stat.h \
	${TOPSRC}/sys/sys/swap.h \
	${TOPSRC}/sys/sys/stdint.h ${TOPSRC}/sys/sys/syscall_dispatch.h \
	${TOPSRC}/sys/sys/syslimits.h ${TOPSRC}/sys/sys/systm.h \
	${TOPSRC}/sys/sys/time.h ${TOPSRC}/sys/sys/tty.h \
	${TOPSRC}/sys/sys/ttychars.h ${TOPSRC}/sys/sys/ttydev.h \
	${TOPSRC}/sys/sys/types.h ${TOPSRC}/sys/sys/uio.h \
	${PORT_INCLUDE}/elf_machdep.h ${PORT_INCLUDE}/intr.h \
	${PORT_INCLUDE}/limits.h ${PORT_INCLUDE}/machparam.h \
	${PORT_INCLUDE}/scb.h ${PORT_INCLUDE}/signal.h \
	${PORT_INCLUDE}/types.h ${MACHINE_STAMP}
FIONREAD_BUILD_TARGETS=	fionread_generic.o fionread_inode.o fionread_test \
	fionread_generic32.o fionread_inode32.o fionread_test32 \
	fionread_test_sanitized \
	${FIONREAD_MUTANTS:@mutation@fionread_${mutation}.mutant.test@}
# Prevent host libc and sanitizer calls from selecting same-named syscall
# handlers out of the production objects. The fixture compiles with the same
# names and calls the renamed handlers directly.
FILE_OWNER_RENAMES=	-Dclose=owner_close -Ddup=owner_dup \
	-Ddup2=owner_dup2 -Dfcntl=owner_fcntl -Dfstat=owner_fstat \
	-Dflock=owner_flock -Dioctl=owner_ioctl -Dlog=owner_log \
	-Dpselect=owner_pselect -Dselect=owner_select \
	-Dread=owner_read -Dreadv=owner_readv \
	-Dwrite=owner_write -Dwritev=owner_writev

# The resource map allocator runs in three shapes across the maintained ports:
# the sixteen-bit descriptors sys/arch/rp2040/compile/PICO configures, the
# wide descriptors STM32 uses, and a DIAGNOSTIC kernel, where
# mfree()'s three overlap panics compile in.
COMPACT_DEFS=		-DCOMPACT_SWAPMAP
WIDE_DEFS=
DIAGNOSTIC_DEFS=	-DCOMPACT_SWAPMAP -DDIAGNOSTIC

PROGS=	rmap_test_compact rmap_test_wide rmap_test_diagnostic uio_test \
	clist_test cred_test resource_test rwuio_test file_owner_test \
	fionread_test swap_ioctl_test

SHUTDOWN_CHECKS=
.if ${MACHINE:U} == "rp2040"
PROGS+=	shutdown_sync_test
SHUTDOWN_CHECKS=	check-shutdown-mutants check-shutdown-order
.endif

SHUTDOWN_MUTANTS=	unconditional_success missing_recount ignored_errors \
		cleared_latch bypassed_guards

# The read, write, readv and writev entry points. rwuio() calls the port's
# setjmp on u_qsave and sleep() longjmps back to it, so rwuio_setjmp.h maps
# the call onto the host's setjmp over a jmp_buf rwuio_jump.c owns, and the
# gate's file operation stub answers through the host's longjmp.
RWUIO_SRC=	${TOPSRC}/sys/kern/sys_generic.c
RWUIO_DEFS=	-include rwuio_setjmp.h
RWUIO_JUMP=	rwuio_jump.c rwuio_jump.h
RWUIO_MUTANTS=	removed_admission first_byte_only

all: ${PROGS}

${MACHINE_STAMP}: hostintr.h ${PORT_INCLUDE}
	@mkdir -p machine
	@rm -f machine/*.h
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

${SWAP_IOCTL_FUNCTION}: swap_ioctl_extract.py ${SWAP_IOCTL_SRC}
	${PYTHON} swap_ioctl_extract.py --source ${SWAP_IOCTL_SRC} --output $@

${SWAP_IOCTL_BUILD_TARGETS}: ${SWAP_IOCTL_HEADER_DEPS}

swap_ioctl_rmap.o: ${RMAP_SRC} hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${SWAP_IOCTL_DEFS} -c -o $@ \
	    ${RMAP_SRC}

swap_ioctl_function.o: ${SWAP_IOCTL_FUNCTION} hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${SWAP_IOCTL_C17_CFLAGS} ${SWAP_IOCTL_DEFS} -c -o $@ \
	    ${SWAP_IOCTL_FUNCTION}

swap_ioctl_test: swap_ioctl_test.c swap_ioctl_function.o swap_ioctl_rmap.o \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${SWAP_IOCTL_DEFS} -o $@ \
	    swap_ioctl_test.c hostkern_kern.c swap_ioctl_function.o \
	    swap_ioctl_rmap.o hostkern.o

swap_ioctl_function32.o: ${SWAP_IOCTL_FUNCTION} hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${SWAP_IOCTL_C17_CFLAGS} ${SWAP_IOCTL_DEFS} ${ILP32} \
	    -c -o $@ ${SWAP_IOCTL_FUNCTION}

swap_ioctl_rmap32.o: ${RMAP_SRC} hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${SWAP_IOCTL_DEFS} ${ILP32} \
	    -c -o $@ ${RMAP_SRC}

swap_ioctl_test32: swap_ioctl_test.c swap_ioctl_function32.o \
    swap_ioctl_rmap32.o hostkern_kern.c hostkern32.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${SWAP_IOCTL_DEFS} ${ILP32} -DHK_ILP32 \
	    -o $@ swap_ioctl_test.c hostkern_kern.c swap_ioctl_function32.o \
	    swap_ioctl_rmap32.o hostkern32.o

swap_ioctl_test_sanitized: swap_ioctl_test.c ${SWAP_IOCTL_FUNCTION} \
    ${RMAP_SRC} hostkern_kern.c hostkern.c hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${SWAP_IOCTL_C17_CFLAGS} ${SWAP_IOCTL_DEFS} \
	    ${KERNEL_SANITIZER_FLAGS} -c -o swap_ioctl_function_sanitized.o \
	    ${SWAP_IOCTL_FUNCTION}
	${HOST_CC} ${KERN_CFLAGS} ${SWAP_IOCTL_DEFS} \
	    ${KERNEL_SANITIZER_FLAGS} -c -o swap_ioctl_rmap_sanitized.o \
	    ${RMAP_SRC}
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o swap_ioctl_hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} ${SWAP_IOCTL_DEFS} \
	    ${KERNEL_SANITIZER_FLAGS} -o $@ swap_ioctl_test.c hostkern_kern.c \
	    swap_ioctl_function_sanitized.o swap_ioctl_rmap_sanitized.o \
	    swap_ioctl_hostkern_sanitized.o

check-swap-ioctl-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-swap-ioctl-sanitizer: swap_ioctl_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.swap-ioctl.sanitized -- \
	    ./swap_ioctl_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.swap-ioctl.sanitized \
	    "swap ioctl contracts: sanitizer runtime unavailable"
.endif

.for mutation in ${SWAP_IOCTL_MUTANTS}
swap_ioctl_${mutation}.mutant.c: swap_ioctl_mutate.py \
    ${SWAP_IOCTL_FUNCTION}
	${PYTHON} swap_ioctl_mutate.py --source ${SWAP_IOCTL_FUNCTION} \
	    --output $@ --mutation ${mutation}

swap_ioctl_${mutation}.mutant.test: swap_ioctl_${mutation}.mutant.c \
    swap_ioctl_test.c swap_ioctl_rmap.o hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${SWAP_IOCTL_C17_CFLAGS} ${SWAP_IOCTL_DEFS} -c \
	    -o swap_ioctl_${mutation}.mutant.o \
	    swap_ioctl_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${SWAP_IOCTL_DEFS} -o $@ \
	    swap_ioctl_test.c hostkern_kern.c \
	    swap_ioctl_${mutation}.mutant.o swap_ioctl_rmap.o hostkern.o
.endfor

check-swap-ioctl-mutants: \
    ${SWAP_IOCTL_MUTANTS:@mutation@swap_ioctl_${mutation}.mutant.test@}
	@set -eu; for mutation in ${SWAP_IOCTL_MUTANTS}; do \
		output=swap_ioctl_$$mutation.mutant.out; \
		if ./swap_ioctl_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		release-fallthrough) expected='call_ioctl(0, TFALLOC, &blocks, FREAD) == 0' ;; \
		media-size-fallthrough) expected='call_ioctl(1, DIOCGETMEDIASIZE, &media_size, FREAD) == 0' ;; \
		shortage-output-suppression) expected='blocks == 0' ;; \
		unrounded-free) expected='free_total() == MAP_BLOCKS' ;; \
		stale-released-size) expected='swap_ioctl_test_size(0) == 0' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL swap ioctl $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS swap ioctl $$mutation mutation rejected"; \
	done

uio_test: uio_test.c ${UIO_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -Wold-style-definition -c -o uio_kernel.o ${UIO_SRC}
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    uio_test.c hostkern_kern.c uio_kernel.o hostkern.o

shutdown_sync_kernel.o: ${SHUTDOWN_SYNC_SRC} hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o $@ ${SHUTDOWN_SYNC_SRC}

shutdown_sync_test: shutdown_sync_test.c shutdown_sync_kernel.o \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} -o $@ shutdown_sync_test.c \
	    shutdown_sync_kernel.o hostkern_kern.c hostkern.o

check-shutdown-mutants: shutdown_sync_test shutdown_sync_mutate.py
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for check-shutdown-mutants" >&2; \
		exit 2; \
	fi
	${PYTHON} shutdown_sync_mutate.py ${SHUTDOWN_SYNC_SRC} ${.CURDIR}
	@set -eu; for mutation in ${SHUTDOWN_MUTANTS}; do \
		${HOST_CC} ${KERN_CFLAGS} -c \
		    -o shutdown_sync_$$mutation.mutant.o \
		    shutdown_sync_$$mutation.mutant.c; \
		${HOST_CC} ${GATE_CFLAGS} \
		    -o shutdown_sync_$$mutation.mutant.test \
		    shutdown_sync_test.c shutdown_sync_$$mutation.mutant.o \
		    hostkern_kern.c hostkern.o; \
		if ./shutdown_sync_$$mutation.mutant.test; then status=0; \
		else status=$$?; fi; \
		if [ "$$status" -ne 1 ]; then \
			echo "FAIL $$mutation returned $$status, expected 1" >&2; \
			exit 1; \
		fi; \
		echo "PASS shutdown-sync mutation $$mutation rejected"; \
	done

check-shutdown-order: shutdown_sync_order_test.py ${MACHDEP_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for check-shutdown-order" >&2; \
		exit 2; \
	fi
	${PYTHON} shutdown_sync_order_test.py ${MACHDEP_SRC}

check-shutdown: shutdown_sync_test check-shutdown-mutants check-shutdown-order
	./shutdown_sync_test

clist_test: clist_test.c ${CLIST_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o clist_kernel.o ${CLIST_SRC}
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    clist_test.c hostkern_kern.c clist_kernel.o hostkern.o

cred_test: cred_test.c ${CRED_SRC} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${CRED_KERN_CFLAGS} -c -o cred_prot.o \
	    ${TOPSRC}/sys/kern/kern_prot.c
	${HOST_CC} ${CRED_KERN_CFLAGS} -c -o cred_prot2.o \
	    ${TOPSRC}/sys/kern/kern_prot2.c
	${HOST_CC} ${KERN_CFLAGS} -c -o cred_proc.o ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ cred_test.c hostkern_kern.c \
	    cred_prot.o cred_prot2.o cred_proc.o hostkern.o

cred_test_sanitized: cred_test.c ${CRED_SRC} hostkern_kern.c hostkern.c \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${CRED_KERN_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o cred_prot_sanitized.o ${TOPSRC}/sys/kern/kern_prot.c
	${HOST_CC} ${CRED_KERN_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o cred_prot2_sanitized.o ${TOPSRC}/sys/kern/kern_prot2.c
	${HOST_CC} ${KERN_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o cred_proc_sanitized.o ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} ${KERNEL_SANITIZER_FLAGS} -o $@ \
	    cred_test.c hostkern_kern.c cred_prot_sanitized.o \
	    cred_prot2_sanitized.o cred_proc_sanitized.o hostkern_sanitized.o

check-credential-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
	@${MAKE} cred_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.credentials.sanitized -- \
	    ./cred_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.credentials.sanitized \
	    "credential contracts: sanitizer runtime unavailable"
.endif

resource_test: resource_test.c ${TOPSRC}/sys/kern/kern_resource.c \
    ${TOPSRC}/sys/kern/kern_proc.c hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} -c -o resource_kernel.o \
	    ${TOPSRC}/sys/kern/kern_resource.c
	${HOST_CC} ${KERN_CFLAGS} -c -o resource_proc.o \
	    ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ resource_test.c hostkern_kern.c \
	    resource_kernel.o resource_proc.o hostkern.o

rwuio_jump.o: ${RWUIO_JUMP}
	${HOST_CC} ${HARNESS_CFLAGS} -c -o $@ rwuio_jump.c

rwuio_test: rwuio_test.c rwuio_setjmp.h rwuio_jump.o ${RWUIO_SRC} \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${RWUIO_DEFS} -c -o rwuio_kernel.o ${RWUIO_SRC}
	${HOST_CC} ${GATE_CFLAGS} -o $@ \
	    rwuio_test.c hostkern_kern.c rwuio_kernel.o rwuio_jump.o hostkern.o

.for mutation in ${RWUIO_MUTANTS}
rwuio_${mutation}.mutant.c: rwuio_mutate.py ${RWUIO_SRC}
	${PYTHON} rwuio_mutate.py --source ${RWUIO_SRC} --output $@ \
	    --mutation ${mutation}

rwuio_${mutation}.mutant.test: rwuio_${mutation}.mutant.c rwuio_test.c \
    rwuio_setjmp.h rwuio_jump.o hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${RWUIO_DEFS} -c \
	    -o rwuio_${mutation}.mutant.o rwuio_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} -o $@ rwuio_test.c hostkern_kern.c \
	    rwuio_${mutation}.mutant.o rwuio_jump.o hostkern.o
.endfor

check-rwuio-mutants: ${RWUIO_MUTANTS:@mutation@rwuio_${mutation}.mutant.test@}
	@set -eu; for mutation in ${RWUIO_MUTANTS}; do \
		output=rwuio_$$mutation.mutant.out; \
		if ./rwuio_$$mutation.mutant.test >$$output 2>&1; then status=0; \
		else status=$$?; fi; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "u.u_error == EFAULT && rw_calls == 0" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL rwuio $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS rwuio $$mutation mutation rejected"; \
	done

rwuio_test_sanitized: rwuio_test.c rwuio_setjmp.h ${RWUIO_JUMP} \
    ${RWUIO_SRC} hostkern_kern.c hostkern.c hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${RWUIO_DEFS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o rwuio_kernel_sanitized.o ${RWUIO_SRC}
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o rwuio_jump_sanitized.o rwuio_jump.c
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o rwuio_hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} ${KERNEL_SANITIZER_FLAGS} -o $@ \
	    rwuio_test.c hostkern_kern.c rwuio_kernel_sanitized.o \
	    rwuio_jump_sanitized.o rwuio_hostkern_sanitized.o

check-rwuio-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
	@${MAKE} rwuio_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.rwuio.sanitized -- \
	    ./rwuio_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.rwuio.sanitized \
	    "rwuio contracts: sanitizer runtime unavailable"
.endif

file_owner_descrip.o: ${FILE_OWNER_DESCRIP_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} ${FILE_OWNER_RENAMES} -c -o $@ \
	    ${FILE_OWNER_DESCRIP_SRC}

file_owner_generic.o: ${FILE_OWNER_GENERIC_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} ${FILE_OWNER_RENAMES} -c -o $@ \
	    ${FILE_OWNER_GENERIC_SRC}

file_owner_test: file_owner_test.c file_owner_descrip.o file_owner_generic.o \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} ${FILE_OWNER_LDFLAGS} -o $@ \
	    file_owner_test.c hostkern_kern.c file_owner_descrip.o \
	    file_owner_generic.o hostkern.o

file_owner_descrip32.o: ${FILE_OWNER_DESCRIP_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} ${ILP32} -c -o $@ \
	    ${FILE_OWNER_DESCRIP_SRC}

file_owner_generic32.o: ${FILE_OWNER_GENERIC_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} ${ILP32} -c -o $@ \
	    ${FILE_OWNER_GENERIC_SRC}

file_owner_test32: file_owner_test.c file_owner_descrip32.o \
    file_owner_generic32.o hostkern_kern.c hostkern32.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} ${ILP32} \
	    -DHK_ILP32 ${FILE_OWNER_LDFLAGS} -o $@ file_owner_test.c \
	    hostkern_kern.c file_owner_descrip32.o file_owner_generic32.o \
	    hostkern32.o

file_owner_test_sanitized: file_owner_test.c ${FILE_OWNER_DESCRIP_SRC} \
    ${FILE_OWNER_GENERIC_SRC} hostkern_kern.c hostkern.c hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${KERNEL_SANITIZER_FLAGS} -c -o file_owner_descrip_sanitized.o \
	    ${FILE_OWNER_DESCRIP_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${KERNEL_SANITIZER_FLAGS} -c -o file_owner_generic_sanitized.o \
	    ${FILE_OWNER_GENERIC_SRC}
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o file_owner_hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${KERNEL_SANITIZER_FLAGS} ${FILE_OWNER_LDFLAGS} -o $@ \
	    file_owner_test.c hostkern_kern.c \
	    file_owner_descrip_sanitized.o file_owner_generic_sanitized.o \
	    file_owner_hostkern_sanitized.o

${FIONREAD_IOCTL_FUNCTION}: fionread_extract.py ${FIONREAD_GENERIC_SRC}
	${PYTHON} fionread_extract.py --function ioctl \
	    --source ${FIONREAD_GENERIC_SRC} --output $@

fionread_generic.o: ${FIONREAD_IOCTL_FUNCTION} ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} \
	    ${FIONREAD_GENERIC_RENAMES} -c -o $@ ${FIONREAD_IOCTL_FUNCTION}

${FIONREAD_INODE_FUNCTION}: fionread_extract.py ${FIONREAD_INODE_SRC}
	${PYTHON} fionread_extract.py --function ino_ioctl \
	    --source ${FIONREAD_INODE_SRC} --output $@

${FIONREAD_BUILD_TARGETS}: ${FIONREAD_HEADER_DEPS}

fionread_inode.o: ${FIONREAD_INODE_FUNCTION} ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} \
	    ${FIONREAD_INODE_RENAMES} -c -o $@ ${FIONREAD_INODE_FUNCTION}

fionread_test: fionread_test.c fionread_generic.o fionread_inode.o \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} -Dioctl=fionread_ioctl \
	    ${FIONREAD_LDFLAGS} -o $@ fionread_test.c hostkern_kern.c \
	    fionread_generic.o fionread_inode.o hostkern.o

fionread_generic32.o: ${FIONREAD_IOCTL_FUNCTION} ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} ${ILP32} \
	    ${FIONREAD_GENERIC_RENAMES} -c -o $@ ${FIONREAD_IOCTL_FUNCTION}

fionread_inode32.o: ${FIONREAD_INODE_FUNCTION} ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} ${ILP32} \
	    ${FIONREAD_INODE_RENAMES} -c -o $@ ${FIONREAD_INODE_FUNCTION}

fionread_test32: fionread_test.c fionread_generic32.o fionread_inode32.o \
    hostkern_kern.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -DHK_ILP32 \
	    -Dioctl=fionread_ioctl ${FIONREAD_LDFLAGS} -o $@ \
	    fionread_test.c hostkern_kern.c fionread_generic32.o \
	    fionread_inode32.o hostkern32.o

fionread_test_sanitized: fionread_test.c ${FIONREAD_IOCTL_FUNCTION} \
    ${FIONREAD_INODE_FUNCTION} hostkern_kern.c hostkern.c hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} \
	    ${FIONREAD_GENERIC_RENAMES} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o fionread_generic_sanitized.o ${FIONREAD_IOCTL_FUNCTION}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} \
	    ${FIONREAD_INODE_RENAMES} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o fionread_inode_sanitized.o ${FIONREAD_INODE_FUNCTION}
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o fionread_hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} -Dioctl=fionread_ioctl \
	    ${KERNEL_SANITIZER_FLAGS} ${FIONREAD_LDFLAGS} -o $@ \
	    fionread_test.c hostkern_kern.c fionread_generic_sanitized.o \
	    fionread_inode_sanitized.o fionread_hostkern_sanitized.o

check-fionread-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-fionread-sanitizer: fionread_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.fionread.sanitized -- \
	    ./fionread_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.fionread.sanitized \
	    "FIONREAD contracts: sanitizer runtime unavailable"
.endif

.for mutation in ${FIONREAD_MUTANTS}
fionread_${mutation}.mutant.c: fionread_mutate.py \
    ${FIONREAD_IOCTL_FUNCTION}
	${PYTHON} fionread_mutate.py --source ${FIONREAD_IOCTL_FUNCTION} \
	    --output $@ --mutation ${mutation}

fionread_${mutation}.mutant.test: fionread_${mutation}.mutant.c \
    fionread_test.c fionread_inode.o hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${FIONREAD_C17_CFLAGS} ${FIONREAD_CFLAGS} \
	    ${FIONREAD_GENERIC_RENAMES} -c \
	    -o fionread_${mutation}.mutant.o fionread_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} -Dioctl=fionread_ioctl \
	    ${FIONREAD_LDFLAGS} -o $@ fionread_test.c hostkern_kern.c \
	    fionread_${mutation}.mutant.o fionread_inode.o hostkern.o
.endfor

check-fionread-mutants: \
    ${FIONREAD_MUTANTS:@mutation@fionread_${mutation}.mutant.test@}
	@set -eu; for mutation in ${FIONREAD_MUTANTS}; do \
		output=fionread_$$mutation.mutant.out; \
		if ./fionread_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		direct-user-pointer) expected='provider_data != unaligned' ;; \
		unconditional-publication) expected='u.u_error == EIO && provider_calls == 1' ;; \
		wrong-width) expected='copyout_bytes == sizeof(off_t)' ;; \
		unmasked-size) expected='provider_calls == 1 && provider_command == command' ;; \
		endpoint-span) expected='u.u_error == ENOTTY && provider_calls == 1' ;; \
		direct-input-pointer) expected='provider_data != unaligned' ;; \
		short-input-copy) expected='copyin_calls == 1 && copyin_bytes == cases[index].nbytes' ;; \
		masked-input-command) expected='provider_command == command && provider_data == argument' ;; \
		unconditional-inout-publication) expected='u.u_error == EINVAL && provider_calls == 1' ;; \
		omitted-inout-publication) expected='copyout_calls == 1 && copyout_bytes == sizeof requested' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL FIONREAD $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS FIONREAD $$mutation mutation rejected"; \
	done

.for mutation in ${FILE_OWNER_UBSAN_MUTANTS}
file_owner_${mutation}.mutant.c: file_owner_mutate.py \
    ${FILE_OWNER_DESCRIP_SRC} ${FILE_OWNER_GENERIC_SRC}
	${PYTHON} file_owner_mutate.py \
	    --descriptor-source ${FILE_OWNER_DESCRIP_SRC} \
	    --generic-source ${FILE_OWNER_GENERIC_SRC} \
	    --output $@ --mutation ${mutation}

file_owner_${mutation}.mutant.test: file_owner_${mutation}.mutant.c \
    file_owner_test.c file_owner_test_sanitized hostkern_kern.c \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o file_owner_${mutation}.mutant.o \
	    file_owner_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${KERNEL_SANITIZER_FLAGS} ${FILE_OWNER_LDFLAGS} -o $@ \
	    file_owner_test.c hostkern_kern.c \
	    file_owner_${mutation}.mutant.o file_owner_generic_sanitized.o \
	    file_owner_hostkern_sanitized.o
.endfor

.for mutation in ${FILE_OWNER_DESCRIP_MUTANTS}
file_owner_${mutation}.mutant.c: file_owner_mutate.py \
    ${FILE_OWNER_DESCRIP_SRC} ${FILE_OWNER_GENERIC_SRC}
	${PYTHON} file_owner_mutate.py \
	    --descriptor-source ${FILE_OWNER_DESCRIP_SRC} \
	    --generic-source ${FILE_OWNER_GENERIC_SRC} \
	    --output $@ --mutation ${mutation}

file_owner_${mutation}.mutant.test: file_owner_${mutation}.mutant.c \
    file_owner_test.c file_owner_generic.o hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} -c -o file_owner_${mutation}.mutant.o \
	    file_owner_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${FILE_OWNER_LDFLAGS} -o $@ file_owner_test.c hostkern_kern.c \
	    file_owner_${mutation}.mutant.o file_owner_generic.o hostkern.o
.endfor

.for mutation in ${FILE_OWNER_GENERIC_MUTANTS}
file_owner_${mutation}.mutant.c: file_owner_mutate.py \
    ${FILE_OWNER_DESCRIP_SRC} ${FILE_OWNER_GENERIC_SRC}
	${PYTHON} file_owner_mutate.py \
	    --descriptor-source ${FILE_OWNER_DESCRIP_SRC} \
	    --generic-source ${FILE_OWNER_GENERIC_SRC} \
	    --output $@ --mutation ${mutation}

file_owner_${mutation}.mutant.test: file_owner_${mutation}.mutant.c \
    file_owner_test.c file_owner_descrip.o hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} -c -o file_owner_${mutation}.mutant.o \
	    file_owner_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${FILE_OWNER_LDFLAGS} -o $@ file_owner_test.c hostkern_kern.c \
	    file_owner_descrip.o file_owner_${mutation}.mutant.o hostkern.o
.endfor

check-file-owner-native-mutants: \
    ${FILE_OWNER_NATIVE_MUTANTS:@mutation@file_owner_${mutation}.mutant.test@}
	@set -eu; for mutation in ${FILE_OWNER_NATIVE_MUTANTS}; do \
		output=file_owner_$$mutation.mutant.out; \
		if ./file_owner_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		unconditional-publication) expected='error == ENOTTY && result == 91 && driver_calls == 1' ;; \
		direct-user-pointer) expected='copyin_calls == 1 && copyout_calls == 0' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL file-owner $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS file-owner $$mutation mutation rejected"; \
	done

check-file-owner-ubsan-mutant: check-file-owner-ubsan-oracle

check-file-owner-ubsan-oracle: file_owner_ubsan_mutant_check.sh \
    file_owner_ubsan_mutant_check_test.sh
	sh ./file_owner_ubsan_mutant_check_test.sh \
	    ${.CURDIR}/file_owner_ubsan_mutant_check.sh

check-file-owner-ubsan-mutant:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-file-owner-ubsan-mutant: file_owner_unchecked-negation.mutant.test \
    file_owner_ubsan_mutant_check.sh
	${TEST_EXECUTION} run kernel.file-owner.ubsan-mutant -- \
	    sh ./file_owner_ubsan_mutant_check.sh \
	    ./file_owner_unchecked-negation.mutant.test
.else
	${TEST_EXECUTION} skip kernel.file-owner.ubsan-mutant \
	    "file-owner unchecked-negation mutation: sanitizer runtime unavailable"
.endif

check-file-owner-mutants: check-file-owner-native-mutants \
    check-file-owner-ubsan-mutant check-file-owner-linker-flags

check-file-owner-linker-flags: file_owner_linker_flags_test.sh
	@set -eu; \
	if sh ./file_owner_linker_flags_test.sh Darwin -Wl,--gc-sections \
	    >/dev/null 2>&1; then \
		echo "file-owner linker flags: accepted the Darwin negative control" >&2; \
		exit 1; \
	fi; \
	sh ./file_owner_linker_flags_test.sh Darwin -Wl,-dead_strip; \
	sh ./file_owner_linker_flags_test.sh Linux -Wl,--gc-sections; \
	darwin_flags=`env MAKEFLAGS= MFLAGS= ${MAKE:T} -V FILE_OWNER_LDFLAGS \
	    _HOST_OSNAME=Darwin MACHINE=${MACHINE:Q}`; \
	elf_flags=`env MAKEFLAGS= MFLAGS= ${MAKE:T} -V FILE_OWNER_LDFLAGS \
	    _HOST_OSNAME=Linux MACHINE=${MACHINE:Q}`; \
	sh ./file_owner_linker_flags_test.sh Darwin "$$darwin_flags"; \
	sh ./file_owner_linker_flags_test.sh Linux "$$elf_flags"; \
	echo "file-owner linker flag selection passed"

check-file-owner-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-file-owner-sanitizer: file_owner_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.file-owner.sanitized -- \
	    ./file_owner_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.file-owner.sanitized \
	    "file-owner contracts: sanitizer runtime unavailable"
.endif

.for mutation in ${FILE_STATUS_DESCRIP_MUTANTS}
file_status_${mutation}.mutant.c: file_status_mutate.py \
    ${FILE_OWNER_DESCRIP_SRC} ${FILE_OWNER_GENERIC_SRC}
	${PYTHON} file_status_mutate.py \
	    --descriptor-source ${FILE_OWNER_DESCRIP_SRC} \
	    --generic-source ${FILE_OWNER_GENERIC_SRC} \
	    --output $@ --mutation ${mutation}

file_status_${mutation}.mutant.test: file_status_${mutation}.mutant.c \
    file_owner_test.c file_owner_generic.o hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} -c -o file_status_${mutation}.mutant.o \
	    file_status_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${FILE_OWNER_LDFLAGS} -o $@ file_owner_test.c hostkern_kern.c \
	    file_status_${mutation}.mutant.o file_owner_generic.o hostkern.o
.endfor

.for mutation in ${FILE_STATUS_GENERIC_MUTANTS}
file_status_${mutation}.mutant.c: file_status_mutate.py \
    ${FILE_OWNER_DESCRIP_SRC} ${FILE_OWNER_GENERIC_SRC}
	${PYTHON} file_status_mutate.py \
	    --descriptor-source ${FILE_OWNER_DESCRIP_SRC} \
	    --generic-source ${FILE_OWNER_GENERIC_SRC} \
	    --output $@ --mutation ${mutation}

file_status_${mutation}.mutant.test: file_status_${mutation}.mutant.c \
    file_owner_test.c file_owner_descrip.o hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${FILE_OWNER_CFLAGS} \
	    ${FILE_OWNER_RENAMES} -c -o file_status_${mutation}.mutant.o \
	    file_status_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${FILE_OWNER_RENAMES} \
	    ${FILE_OWNER_LDFLAGS} -o $@ file_owner_test.c hostkern_kern.c \
	    file_owner_descrip.o file_status_${mutation}.mutant.o hostkern.o
.endfor

check-file-status-mutants: \
    ${FILE_STATUS_MUTANTS:@mutation@file_status_${mutation}.mutant.test@}
	@set -eu; for mutation in ${FILE_STATUS_MUTANTS}; do \
		output=file_status_$$mutation.mutant.out; \
		if ./file_status_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		early-fset-publication) expected='driver_flags[0] == (FREAD | FWRITE)' ;; \
		early-setfl-publication) expected='driver_flags[0] == old_flags' ;; \
		zero-setfl-rollback) expected='driver_inputs[2] != 0' ;; \
		direct-status-pointer) expected='copyin_calls == 1 && driver_calls == 1' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL file-status $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS file-status $$mutation mutation rejected"; \
	done

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
	${HOST_CC} ${CRED_KERN_CFLAGS} ${ILP32} -c -o sigauth_prot2.o \
	    ${TOPSRC}/sys/kern/kern_prot2.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_proc.o \
	    ${TOPSRC}/sys/kern/kern_proc.c
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o sigauth_shim.o hostkern_kern.c
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -o $@ sigauth_test.c \
	    sigauth_sig.o sigauth_prot2.o sigauth_proc.o sigauth_shim.o hostkern32.o

# The node's extent is a count of mapents, so the gate runs in both
# descriptor shapes: the sixteen-bit entries PICO configures and the wide
# entries STM32 uses, as rmap_test does.
sysctl_shim.o: hostkern_kern.c hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} -c -o $@ hostkern_kern.c

sysctl_test_compact: sysctl_test.c ${SYSCTL_SRC} sysctl_shim.o hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${COMPACT_DEFS} ${ILP32} \
	    -c -o sysctl_kernel_compact.o ${SYSCTL_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${COMPACT_DEFS} ${ILP32} -o $@ sysctl_test.c \
	    sysctl_kernel_compact.o sysctl_shim.o hostkern32.o

sysctl_test_wide: sysctl_test.c ${SYSCTL_SRC} sysctl_shim.o hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${WIDE_DEFS} ${ILP32} \
	    -c -o sysctl_kernel_wide.o ${SYSCTL_SRC}
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

ufs_explicit_time_cred.o: ${UFS_EXPLICIT_TIME_CRED_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${CRED_KERN_CFLAGS} ${UFS_EXPLICIT_TIME_DEFS} ${ILP32} \
	    ${UFS_EXPLICIT_TIME_CFLAGS} -c -o $@ ${UFS_EXPLICIT_TIME_CRED_SRC}

ufs_explicit_time_test: ufs_explicit_time_test.c ${UFS_EXPLICIT_TIME_SRC} \
    ufs_explicit_time_cred.o hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_EXPLICIT_TIME_DEFS} ${ILP32} \
	    ${UFS_EXPLICIT_TIME_CFLAGS} -c -o ufs_explicit_time_kernel.o \
	    ${UFS_EXPLICIT_TIME_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${UFS_EXPLICIT_TIME_DEFS} ${ILP32} \
	    ${UFS_EXPLICIT_TIME_LDFLAGS} -o $@ ufs_explicit_time_test.c \
	    ufs_explicit_time_kernel.o ufs_explicit_time_cred.o hostkern32.o

capacity_test: capacity_test.c capacity_uarea.c ${CAPACITY_SRC} hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${CAPACITY_DEFS} ${ILP32} \
	    -c -o capacity_kernel.o ${CAPACITY_SRC}
	${HOST_CC} ${KERN_CFLAGS} ${CAPACITY_DEFS} ${ILP32} \
	    -c -o capacity_uarea.o capacity_uarea.c
	${HOST_CC} ${GATE_CFLAGS} ${CAPACITY_DEFS} ${ILP32} -o $@ \
	    capacity_test.c capacity_kernel.o capacity_uarea.o hostkern32.o

check-capacity-mutants: capacity_test capacity_mutate.py
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for check-capacity-mutants" >&2; \
		exit 2; \
	fi
	${PYTHON} capacity_mutate.py ${CAPACITY_SRC} ${.CURDIR}
	@set -eu; for mutation in ${CAPACITY_MUTANTS}; do \
		${HOST_CC} ${KERN_CFLAGS} ${CAPACITY_DEFS} ${ILP32} \
		    -c -o capacity_$$mutation.mutant.o \
		    capacity_$$mutation.mutant.c; \
		${HOST_CC} ${GATE_CFLAGS} ${CAPACITY_DEFS} ${ILP32} \
		    -o capacity_$$mutation.mutant.test capacity_test.c \
		    capacity_$$mutation.mutant.o capacity_uarea.o hostkern32.o; \
		if ./capacity_$$mutation.mutant.test; then status=0; \
		else status=$$?; fi; \
		if [ "$$status" -ne 1 ]; then \
			echo "FAIL capacity mutation $$mutation returned $$status, expected 1" >&2; \
			exit 1; \
		fi; \
		echo "PASS capacity mutation $$mutation rejected"; \
	done

# Restore the inherited defect in a generated source copy. The same executable
# must reject a MNT_NOATIME mount that drops an explicit atime request.
ufs_fio_noatime_bad.c: ${UFS_EXPLICIT_TIME_SRC}
	@awk 'BEGIN { changed = 0 } \
	    !changed && $$0 == "            ip->i_flag |= IACC;" { \
	        print "            if ((INODE_FILESYSTEM(ip)->fs_flags & MNT_NOATIME) == 0)"; \
	        print "                ip->i_flag |= IACC;"; changed = 1; next \
	    } \
	    { print } \
	    END { if (!changed) exit 1 }' ${UFS_EXPLICIT_TIME_SRC} > $@

ufs_explicit_time_bad: ufs_explicit_time_test.c ufs_fio_noatime_bad.c \
    ufs_explicit_time_cred.o hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_EXPLICIT_TIME_DEFS} ${ILP32} \
	    ${UFS_EXPLICIT_TIME_CFLAGS} -c -o ufs_explicit_time_bad_kernel.o \
	    ufs_fio_noatime_bad.c
	${HOST_CC} ${GATE_CFLAGS} ${UFS_EXPLICIT_TIME_DEFS} ${ILP32} \
	    ${UFS_EXPLICIT_TIME_LDFLAGS} -o $@ ufs_explicit_time_test.c \
	    ufs_explicit_time_bad_kernel.o ufs_explicit_time_cred.o hostkern32.o

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

usercopy_functions.c: usercopy_extract.py ${USERCOPY_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for usercopy extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} usercopy_extract.py --source ${USERCOPY_SRC} --output $@

usercopy_endpoint_mutant.c: usercopy_extract.py ${USERCOPY_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for usercopy extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} usercopy_extract.py --source ${USERCOPY_SRC} --output $@ \
	    --endpoint-mutant

usercopy_span_test32: usercopy_span_test.c usercopy_functions.c \
    ${USERCOPY_HEADER} ${MACHINE_STAMP}
	${HOST_CC} ${HARNESS_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys \
	    -I${TOPSRC}/sys/arch -o $@ usercopy_span_test.c

usercopy_span_endpoint_mutant: usercopy_span_test.c \
    usercopy_endpoint_mutant.c ${USERCOPY_HEADER} ${MACHINE_STAMP}
	${HOST_CC} ${HARNESS_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys \
	    -I${TOPSRC}/sys/arch \
	    -DUSERCOPY_FUNCTIONS='"usercopy_endpoint_mutant.c"' \
	    -o $@ usercopy_span_test.c

syscall_args_block.c: syscall_args_extract.py ${SYSCALL_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for syscall extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} syscall_args_extract.py --source ${SYSCALL_SRC} --output $@

syscall_args_dispatch_mutant.c: syscall_args_extract.py ${SYSCALL_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for syscall extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} syscall_args_extract.py --source ${SYSCALL_SRC} --output $@ \
	    --dispatch-after-failure-mutant

syscall_args_test32: syscall_args_test.c syscall_args_block.c
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} -o $@ syscall_args_test.c

syscall_args_dispatch_mutant: syscall_args_test.c \
    syscall_args_dispatch_mutant.c
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -DSYSCALL_ARGS_BLOCK='"syscall_args_dispatch_mutant.c"' \
	    -o $@ syscall_args_test.c

sigreturn_context_function.c: sigreturn_context_extract.py ${SIGRETURN_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for sigreturn extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} sigreturn_context_extract.py --source ${SIGRETURN_SRC} \
	    --output $@

sigreturn_context_direct_mutant.c: sigreturn_context_extract.py \
    ${SIGRETURN_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for sigreturn extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} sigreturn_context_extract.py --source ${SIGRETURN_SRC} \
	    --output $@ --direct-user-mutant

sigreturn_context_reread_mutant.c: sigreturn_context_extract.py \
    ${SIGRETURN_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for sigreturn extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} sigreturn_context_extract.py --source ${SIGRETURN_SRC} \
	    --output $@ --reread-user-mutant

sigreturn_context_test32: sigreturn_context_test.c \
    sigreturn_context_function.c
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -o $@ sigreturn_context_test.c

sigreturn_context_direct_mutant: sigreturn_context_test.c \
    sigreturn_context_direct_mutant.c
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -DSIGRETURN_CONTEXT_FUNCTION='"sigreturn_context_direct_mutant.c"' \
	    -o $@ sigreturn_context_test.c

sigreturn_context_reread_mutant: sigreturn_context_test.c \
    sigreturn_context_reread_mutant.c
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -DSIGRETURN_CONTEXT_FUNCTION='"sigreturn_context_reread_mutant.c"' \
	    -o $@ sigreturn_context_test.c

exec_elf_admission_function.c: exec_elf_admission_extract.py ${EXEC_ELF_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for ELF loader extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_ELF_SRC} \
	    --output $@

exec_elf_segment_span_mutant.c: exec_elf_admission_extract.py ${EXEC_ELF_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for ELF loader extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_ELF_SRC} \
	    --output $@ --segment-span-mutant

exec_elf_read_failure_mutant.c: exec_elf_admission_extract.py ${EXEC_ELF_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for ELF loader extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_ELF_SRC} \
	    --output $@ --read-failure-mutant

exec_finish_function.c: exec_elf_admission_extract.py ${KERN_EXEC_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for exec completion extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${KERN_EXEC_SRC} \
	    --output $@ --finish

exec_finish_signal_mutant.c: exec_elf_admission_extract.py ${KERN_EXEC_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for exec completion extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${KERN_EXEC_SRC} \
	    --output $@ --finish-signal-mutant

exec_estab_function.c: exec_elf_admission_extract.py ${EXEC_SUBR_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for exec admission extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_SUBR_SRC} \
	    --output $@ --establish

exec_setupstack_function.c: exec_elf_admission_extract.py ${EXEC_SUBR_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for stack publisher extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_SUBR_SRC} \
	    --output $@ --setupstack

exec_setupstack_geometry_mutant.c: exec_elf_admission_extract.py ${EXEC_SUBR_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for stack mutation" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_SUBR_SRC} \
	    --output $@ --setupstack-geometry-mutant

exec_elf_text_ownership_mutant.c: exec_elf_admission_extract.py ${EXEC_ELF_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for ELF ownership mutation" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_ELF_SRC} \
	    --output $@ --text-ownership-mutant

swaptext_function.c: exec_elf_admission_extract.py ${VM_SWAP_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for swap text extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${VM_SWAP_SRC} \
	    --output $@ --swaptext

exec_text_release_function.c: exec_elf_admission_extract.py ${EXEC_AOUT_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for text-owner extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} exec_elf_admission_extract.py --source ${EXEC_AOUT_SRC} \
	    --output $@ --text-release

exec_elf_surrounding_contract_test32: exec_elf_surrounding_contract_test.c \
    exec_elf_admission_function.c exec_estab_function.c \
    exec_setupstack_function.c swaptext_function.c \
    exec_text_release_function.c ${MACHINE_STAMP}
	${HOST_CC} ${ELF_SURROUNDING_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -o $@ exec_elf_surrounding_contract_test.c

exec_stack_geometry_mutant: exec_elf_surrounding_contract_test.c \
    exec_elf_admission_function.c exec_estab_function.c \
    exec_setupstack_geometry_mutant.c swaptext_function.c \
    exec_text_release_function.c ${MACHINE_STAMP}
	${HOST_CC} ${ELF_SURROUNDING_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -DEXEC_SETUPSTACK_FUNCTION='"exec_setupstack_geometry_mutant.c"' \
	    -o $@ exec_elf_surrounding_contract_test.c

exec_elf_text_ownership_mutant: exec_elf_surrounding_contract_test.c \
    exec_elf_text_ownership_mutant.c exec_estab_function.c \
    exec_setupstack_function.c swaptext_function.c \
    exec_text_release_function.c ${MACHINE_STAMP}
	${HOST_CC} ${ELF_SURROUNDING_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -DEXEC_ELF_FUNCTION='"exec_elf_text_ownership_mutant.c"' \
	    -o $@ exec_elf_surrounding_contract_test.c

exec_elf_admission_test32: exec_elf_admission_test.c \
    exec_elf_admission_function.c exec_finish_function.c ${MACHINE_STAMP}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -o $@ exec_elf_admission_test.c

exec_elf_segment_span_mutant: exec_elf_admission_test.c \
    exec_elf_segment_span_mutant.c exec_finish_function.c ${MACHINE_STAMP}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -DEXEC_ELF_FUNCTION='"exec_elf_segment_span_mutant.c"' \
	    -o $@ exec_elf_admission_test.c

exec_elf_read_failure_mutant: exec_elf_admission_test.c \
    exec_elf_read_failure_mutant.c exec_finish_function.c ${MACHINE_STAMP}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -DEXEC_ELF_FUNCTION='"exec_elf_read_failure_mutant.c"' \
	    -o $@ exec_elf_admission_test.c

exec_finish_signal_mutant: exec_elf_admission_test.c \
    exec_elf_admission_function.c exec_finish_signal_mutant.c ${MACHINE_STAMP}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} -I. -I${TOPSRC}/sys/arch \
	    -DEXEC_FINISH_FUNCTION='"exec_finish_signal_mutant.c"' \
	    -o $@ exec_elf_admission_test.c

# The narrow gate, which the root Makefile reaches as check-kernel-ilp32.
check-ilp32:
.if ${ILP32_OK} == "yes"
	@${MAKE} prf_test rwuio_test32 file_owner_test32 fionread_test32 sigauth_test \
	    swap_ioctl_test32 \
	    usercopy_span_test32 usercopy_span_endpoint_mutant syscall_args_test32 \
	    syscall_args_dispatch_mutant sigreturn_context_test32 \
	    sigreturn_context_direct_mutant sigreturn_context_reread_mutant \
	    exec_elf_admission_test32 exec_elf_segment_span_mutant \
	    exec_elf_read_failure_mutant exec_finish_signal_mutant \
	    exec_elf_surrounding_contract_test32 exec_stack_geometry_mutant \
	    exec_elf_text_ownership_mutant \
	    sysctl_test_compact sysctl_test_wide \
	    ialloc_test ialloc_test_pico ialloc_test_diagnostic \
	    balloc_test balloc_test_pico \
	    ufs_explicit_time_test ufs_explicit_time_bad capacity_test \
	    check-capacity-mutants
	${TEST_EXECUTION} run kernel.printf.ilp32 -- ./prf_test
	${TEST_EXECUTION} run kernel.signal-auth.ilp32 -- ./sigauth_test
	${PYTHON} ${.CURDIR}/sigauth_mutate.py ${TOPSRC} ${HOST_CC:Q} \
	    ${.CURDIR}/sigauth_test.c ${.CURDIR}/sigauth_prot2.o \
	    ${.CURDIR}/sigauth_proc.o ${.CURDIR}/sigauth_shim.o \
	    ${.CURDIR}/hostkern32.o
	${TEST_EXECUTION} run kernel.rwuio.ilp32 -- ./rwuio_test32
	${TEST_EXECUTION} run kernel.file-owner.ilp32 -- ./file_owner_test32
	${TEST_EXECUTION} run kernel.fionread.ilp32 -- ./fionread_test32
	${TEST_EXECUTION} run kernel.swap-ioctl.ilp32 -- ./swap_ioctl_test32
	${TEST_EXECUTION} run kernel.usercopy-span.ilp32 -- ./usercopy_span_test32
	${TEST_EXECUTION} run kernel.syscall-arguments.ilp32 -- ./syscall_args_test32
	@if ./syscall_args_dispatch_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL syscall dispatch mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS syscall dispatch-after-failure mutation rejected"
	${TEST_EXECUTION} run kernel.sigreturn-context.ilp32 -- \
	    ./sigreturn_context_test32
	@if ./sigreturn_context_direct_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL sigreturn direct-user mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS sigreturn direct-user mutation rejected"
	@if ./sigreturn_context_reread_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL sigreturn user-reread mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS sigreturn user-reread mutation rejected"
	${TEST_EXECUTION} run kernel.elf-admission.ilp32 -- \
	    ./exec_elf_admission_test32
	@if ./exec_elf_segment_span_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL ELF segment-span mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS ELF segment-span mutation rejected"
	@if ./exec_elf_read_failure_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL ELF read-failure mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS ELF read-failure mutation rejected"
	@if ./exec_finish_signal_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL exec signal-termination mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS exec signal-termination mutation rejected"
	${TEST_EXECUTION} run kernel.elf-surrounding-contract.ilp32 -- \
	    ./exec_elf_surrounding_contract_test32
	@if ./exec_stack_geometry_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL stack geometry mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS stack geometry mutation rejected"
	@if ./exec_elf_text_ownership_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL ELF text ownership mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS ELF text ownership mutation rejected"
	@if ./usercopy_span_endpoint_mutant; then status=0; else status=$$?; fi; \
	if [ "$$status" -ne 1 ]; then \
		echo "FAIL user-copy endpoint mutation returned $$status, expected 1" >&2; \
		exit 1; \
	fi; \
	echo "PASS user-copy endpoint mutation rejected"
	${TEST_EXECUTION} run kernel.sysctl-compact.ilp32 -- ./sysctl_test_compact
	${TEST_EXECUTION} run kernel.sysctl-wide.ilp32 -- ./sysctl_test_wide
	${TEST_EXECUTION} run kernel.inode.ilp32 -- ./ialloc_test
	${TEST_EXECUTION} run kernel.inode-pico.ilp32 -- ./ialloc_test_pico
	${TEST_EXECUTION} run kernel.inode-diagnostic.ilp32 -- ./ialloc_test_diagnostic
	${TEST_EXECUTION} run kernel.block.ilp32 -- ./balloc_test
	${TEST_EXECUTION} run kernel.block-pico.ilp32 -- ./balloc_test_pico
	${TEST_EXECUTION} run kernel.explicit-time.ilp32 -- ./ufs_explicit_time_test
	${TEST_EXECUTION} run kernel.explicit-time-negative.ilp32 -- ./ufs_explicit_time_bad
	${TEST_EXECUTION} run kernel.capacity.ilp32 -- ./capacity_test
.else
	${TEST_EXECUTION} skip kernel.printf.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.signal-auth.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.rwuio.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.file-owner.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.fionread.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.swap-ioctl.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.usercopy-span.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.syscall-arguments.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.sigreturn-context.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.elf-admission.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.elf-surrounding-contract.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.sysctl-compact.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.sysctl-wide.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.inode.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.inode-pico.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.inode-diagnostic.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.block.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.block-pico.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.explicit-time.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.explicit-time-negative.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.capacity.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
.endif

check: ${PROGS} ${SHUTDOWN_CHECKS} check-credential-sanitizer \
    check-rwuio-mutants check-rwuio-sanitizer \
    check-file-owner-mutants check-file-owner-sanitizer \
    check-file-status-mutants check-fionread-mutants \
    check-fionread-sanitizer check-swap-ioctl-mutants \
    check-swap-ioctl-sanitizer
	@for prog in ${PROGS}; do \
		echo "run ./$$prog"; \
		./$$prog || exit 1; \
	done

clean:
	rm -f ${PROGS} hostkern.o
	rm -f rmap_kernel_compact.o rmap_kernel_wide.o rmap_kernel_diagnostic.o
	rm -f ${SWAP_IOCTL_FUNCTION} swap_ioctl_function.o swap_ioctl_rmap.o
	rm -f swap_ioctl_test32 swap_ioctl_function32.o swap_ioctl_rmap32.o
	rm -f swap_ioctl_test_sanitized swap_ioctl_function_sanitized.o \
	    swap_ioctl_rmap_sanitized.o swap_ioctl_hostkern_sanitized.o
	rm -f swap_ioctl_*.mutant.c swap_ioctl_*.mutant.o \
	    swap_ioctl_*.mutant.test swap_ioctl_*.mutant.out
	rm -f uio_kernel.o clist_kernel.o
	rm -f shutdown_sync_kernel.o shutdown_sync_test
	rm -f shutdown_sync_*.mutant.c shutdown_sync_*.mutant.o \
	    shutdown_sync_*.mutant.test
	rm -f cred_prot.o cred_prot2.o cred_proc.o
	rm -f cred_test_sanitized cred_prot_sanitized.o cred_prot2_sanitized.o \
	    cred_proc_sanitized.o hostkern_sanitized.o
	rm -f resource_kernel.o resource_proc.o
	rm -f prf_kernel.o prf_shim.o hostkern32.o prf_test
	rm -f sigauth_sig.o sigauth_prot2.o sigauth_proc.o sigauth_shim.o sigauth_test
	rm -f rwuio_kernel.o rwuio_kernel32.o rwuio_shim32.o rwuio_test32
	rm -f rwuio_jump.o rwuio_jump32.o
	rm -f rwuio_*.mutant.c rwuio_*.mutant.o rwuio_*.mutant.test \
	    rwuio_*.mutant.out
	rm -f rwuio_test_sanitized rwuio_kernel_sanitized.o \
	    rwuio_jump_sanitized.o rwuio_hostkern_sanitized.o
	rm -f file_owner_test file_owner_descrip.o file_owner_generic.o
	rm -f file_owner_test32 file_owner_descrip32.o file_owner_generic32.o
	rm -f file_owner_test_sanitized file_owner_descrip_sanitized.o \
	    file_owner_generic_sanitized.o file_owner_hostkern_sanitized.o
	rm -f file_owner_*.mutant.c file_owner_*.mutant.o \
	    file_owner_*.mutant.test file_owner_*.mutant.out
	rm -f file_status_*.mutant.c file_status_*.mutant.o \
	    file_status_*.mutant.test file_status_*.mutant.out
	rm -f fionread_test fionread_generic.o fionread_inode.o
	rm -f fionread_test32 fionread_generic32.o fionread_inode32.o
	rm -f fionread_test_sanitized fionread_generic_sanitized.o \
	    fionread_inode_sanitized.o fionread_hostkern_sanitized.o \
	    ${FIONREAD_IOCTL_FUNCTION} ${FIONREAD_INODE_FUNCTION}
	rm -f fionread_*.mutant.c fionread_*.mutant.o \
	    fionread_*.mutant.test fionread_*.mutant.out
	rm -f usercopy_functions.c usercopy_endpoint_mutant.c
	rm -f usercopy_span_test32 usercopy_span_endpoint_mutant
	rm -f syscall_args_block.c syscall_args_dispatch_mutant.c
	rm -f syscall_args_test32 syscall_args_dispatch_mutant
	rm -f sigreturn_context_function.c \
	    sigreturn_context_direct_mutant.c sigreturn_context_reread_mutant.c
	rm -f sigreturn_context_test32 sigreturn_context_direct_mutant \
	    sigreturn_context_reread_mutant
	rm -f exec_elf_admission_function.c exec_elf_segment_span_mutant.c \
	    exec_elf_read_failure_mutant.c exec_finish_function.c \
	    exec_finish_signal_mutant.c exec_estab_function.c \
	    exec_setupstack_function.c exec_setupstack_geometry_mutant.c \
	    exec_elf_text_ownership_mutant.c swaptext_function.c \
	    exec_text_release_function.c
	rm -f exec_elf_admission_test32 exec_elf_segment_span_mutant \
	    exec_elf_read_failure_mutant exec_finish_signal_mutant \
	    exec_elf_surrounding_contract_test32 exec_stack_geometry_mutant \
	    exec_elf_text_ownership_mutant
	rm -f sysctl_kernel_compact.o sysctl_kernel_wide.o sysctl_shim.o
	rm -f sysctl_test_compact sysctl_test_wide
	rm -f ialloc_kernel.o ialloc_kernel_pico.o ialloc_kernel_diagnostic.o
	rm -f ialloc_shim.o ialloc_shim_pico.o
	rm -f ialloc_test ialloc_test_pico ialloc_test_diagnostic
	rm -f balloc_kernel.o balloc_kernel_pico.o
	rm -f balloc_shim.o balloc_shim_pico.o
	rm -f balloc_test balloc_test_pico
	rm -f ufs_explicit_time_kernel.o ufs_explicit_time_bad_kernel.o \
	    ufs_explicit_time_cred.o
	rm -f ufs_explicit_time_test ufs_explicit_time_bad ufs_fio_noatime_bad.c
	rm -f capacity_kernel.o capacity_uarea.o capacity_test
	rm -f capacity_*.mutant.c capacity_*.mutant.o capacity_*.mutant.test
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

.PHONY: all check check-ilp32 check-shutdown check-shutdown-mutants \
	check-shutdown-order check-capacity-mutants check-credential-sanitizer \
	check-rwuio-mutants check-rwuio-sanitizer \
	check-file-owner-mutants check-file-owner-native-mutants \
	check-file-owner-ubsan-mutant check-file-owner-ubsan-oracle \
	check-file-owner-linker-flags \
	check-file-owner-sanitizer check-file-status-mutants \
	check-fionread-mutants check-fionread-sanitizer clean
