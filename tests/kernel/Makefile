TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc

PORT_INCLUDE=	${TOPSRC}/sys/arch/${MACHINE}/include
USERCOPY_HEADER=	${PORT_INCLUDE}/usercopy.h
USER_CONTEXT_HEADER=	${PORT_INCLUDE}/user_context.h

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
SIGRETURN_SANITIZER_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native \
			${HOST_CC} ${ILP32} ${KERNEL_SANITIZER_FLAGS}
TTY_LOCAL_MODE_ILP32_UBSAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native \
			${HOST_CC} ${ILP32} -fsanitize=undefined \
			-fno-sanitize-recover=all

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

# futimes(2) stages the complete timeval pair before descriptor lookup, then
# publishes timestamps to the locked inode owned by that descriptor. Function
# sections isolate the caller-complete syscall from unrelated UFS entry points.
FUTIMES_SRC=	${TOPSRC}/sys/kern/ufs_syscalls2.c
FUTIMES_CFLAGS=	-ffunction-sections -fdata-sections
FUTIMES_RENAMES=	-Dstatfs=futimes_gate_statfs \
	-Dfstatfs=futimes_gate_fstatfs -Dgetfsstat=futimes_gate_getfsstat \
	-Dumask=futimes_gate_umask -Dlseek=futimes_gate_lseek \
	-Dfsync=futimes_gate_fsync -Dutimes=futimes_gate_utimes \
	-Dfutimes=kernel_futimes
FUTIMES_MUTANTS=	short-copy descriptor-before-copy
.if ${_HOST_OSNAME} == "Darwin"
OPENAT_LDFLAGS=	-Wl,-dead_strip
.else
OPENAT_LDFLAGS=	-Wl,--gc-sections
.endif
OPENAT_NAMEI_SRC=	${TOPSRC}/sys/kern/ufs_namei.c
OPENAT_VNOPS_SRC=	${TOPSRC}/sys/kern/vfs_vnops.c
OPENAT_SYSCALL_SRC=	${TOPSRC}/sys/kern/ufs_syscalls.c
OPENAT_DEFS=	-DSINGLE_UFS_ROOT -DNMOUNT=1 -DLINEAR_NAME_CACHE \
		-DNNAMECACHE=4
OPENAT_CFLAGS=	-ffunction-sections -fdata-sections ${OPENAT_DEFS}
OPENAT_MULTIFS_DEFS=	-DNMOUNT=2 -DLINEAR_NAME_CACHE -DNNAMECACHE=4
OPENAT_MULTIFS_CFLAGS=	-ffunction-sections -fdata-sections \
			${OPENAT_MULTIFS_DEFS}
OPENAT_RENAMES=	-Dopen=kernel_open -Dopenat=kernel_openat \
		-Dopenat_beneath=kernel_openat_beneath \
		-Dmkdir=kernel_mkdir \
		-Dmkdirat_beneath=kernel_mkdirat_beneath \
		-Dsetjmp=openat_setjmp -Diput=iput_openat
OPENAT_SYSCALL_RENAMES=	${OPENAT_RENAMES} -Dmaknode=openat_syscalls_maknode
OPENAT_NAMEI_MUTANTS=	cwd-instead-of-dirfd absolute-consults-dirfd \
			accept-empty-beneath follow-beneath-symlink \
			cross-beneath-mount
OPENAT_VNOPS_MUTANTS=	follow-final-symlink \
			hold-namespace-during-open-provider
OPENAT_SYSCALL_MUTANTS=	omit-rooted-open-policy release-before-open \
			omit-rooted-open-release release-competing-open-owner \
			omit-rooted-mkdir-policy \
			release-before-mkdir-publication \
			omit-rooted-mkdir-release omit-mkdir-parent-rollback \
			omit-mkdir-parent-iput
OPENAT_MUTANTS=	${OPENAT_NAMEI_MUTANTS} ${OPENAT_VNOPS_MUTANTS} \
		${OPENAT_SYSCALL_MUTANTS}
UNLINKAT_BENEATH_NAMEI_SRC=	${TOPSRC}/sys/kern/ufs_namei.c
UNLINKAT_BENEATH_SYSCALL_SRC=	${TOPSRC}/sys/kern/ufs_syscalls.c
UNLINKAT_BENEATH_CFLAGS=	-ffunction-sections -fdata-sections \
				${OPENAT_MULTIFS_DEFS}
UNLINKAT_BENEATH_RENAMES=	-Dlink=conditional_unlink_link \
				-Dunlink=conditional_unlink_legacy \
				-Drename=conditional_unlink_rename
UNLINKAT_BENEATH_MUTANTS=	omit-namespace-acquisition \
				release-before-removal skip-identity-comparison \
				drop-expected-reference decrement-on-write-error
PUBLISHAT_BENEATH_SYSCALL_SRC=	${TOPSRC}/sys/kern/ufs_syscalls.c
PUBLISHAT_BENEATH_CFLAGS=	-ffunction-sections -fdata-sections \
				${OPENAT_MULTIFS_DEFS}
PUBLISHAT_BENEATH_MUTANTS=	discard-reserve-write-error \
				skip-staged-identity skip-staged-sticky \
				skip-target-identity use-at-fdcwd \
				release-before-publication skip-same-parent \
				publish-after-remove-error
PUBLISHAT_NAMEI_MUTANTS=	discard-enter-write-error
DIRREMOVE_SRC=	${TOPSRC}/sys/kern/ufs_namei.c
DIRREMOVE_CFLAGS=	-ffunction-sections -fdata-sections ${OPENAT_DEFS}
DIRREMOVE_MUTANTS=	discard-first-write-error discard-coalesced-write-error
UFS_NAMESPACE_SRC=	${TOPSRC}/sys/kern/ufs_namei.c
UFS_NAMESPACE_CFLAGS=	-ffunction-sections -fdata-sections -DDIAGNOSTIC \
			${OPENAT_DEFS}
UFS_NAMESPACE_RENAMES=	-Dcheckpath=namespace_checkpath \
			-Dnamei=namespace_namei -Dnamei_at=namespace_namei_at \
			-Ddirempty=namespace_dirempty \
			-Dcacheinval=namespace_cacheinval \
			-Ddirrewrite=namespace_dirrewrite \
			-Ddirenter=namespace_direnter \
			-Ddirremove=namespace_dirremove
UFS_NAMESPACE_MUTANTS=	omit-acquisition omit-wakeup clear-on-interrupt
UFS_TOPOLOGY_SRC=	${TOPSRC}/sys/kern/ufs_syscalls.c
UFS_TOPOLOGY_CFLAGS=	-ffunction-sections -fdata-sections ${OPENAT_DEFS}
UFS_TOPOLOGY_RENAMES=	-Dlink=kernel_link -Dunlink=kernel_unlink \
			-Drename=kernel_rename -Dnamei=topology_namei
UFS_TOPOLOGY_MUTANTS=	omit-rename-acquisition \
			release-before-source-lookup \
			release-before-directory-checkpath allow-directory-link \
			allow-directory-unlink
.if ${_HOST_OSNAME} == "Darwin"
FUTIMES_LDFLAGS=	-Wl,-dead_strip
.else
FUTIMES_LDFLAGS=	-Wl,--gc-sections
.endif

# The RP2040 shutdown path. The production helper uses bounded flags, counts
# and kernel structure fields, so the host-width gate can exercise its
# sequencing and output; both production configurations compile it separately
# for Cortex-M0+.
SHUTDOWN_SYNC_SRC=	${TOPSRC}/sys/arch/rp2040/rp2040/shutdown_sync.c
MACHDEP_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/machdep.c
USERCOPY_SRC=		${MACHDEP_SRC}
SYSCALL_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/syscall.c
SIGRETURN_SRC=		${TOPSRC}/sys/arch/rp2040/rp2040/sig_machdep.c
SIGRETURN_HEADER_DEPS=	${TOPSRC}/sys/arch/rp2040/include/frame.h \
			${TOPSRC}/sys/arch/rp2040/include/scb.h \
			${TOPSRC}/sys/arch/rp2040/include/signal.h \
			${TOPSRC}/sys/arch/rp2040/include/user_context.h
SIGRETURN_MUTANTS=	direct-user reread-user pc-admission sp-alignment \
			frame-width padding-width psr-policy early-publication
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
				unconditional-inout-publication omitted-inout-publication \
				direct-output-pointer unconditional-output-publication \
				short-output-copy omitted-output-publication \
				corrupt-output-initialization
FIONREAD_HEADER_DEPS=	hostkern.h hostintr.h \
	${TOPSRC}/sys/sys/buf.h ${TOPSRC}/sys/sys/capacity.h \
	${TOPSRC}/sys/sys/cdefs.h ${TOPSRC}/sys/sys/conf.h \
		${TOPSRC}/sys/sys/dir.h ${TOPSRC}/sys/sys/disk.h \
		${TOPSRC}/sys/sys/errno.h \
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
TTY_SRC=		${TOPSRC}/sys/kern/tty.c
TTY_LOCAL_MODE_FUNCTIONS=	tty_local_mode_functions.c
TTY_LOCAL_MODE_CFLAGS=	-std=c17 -pedantic-errors ${GATE_WARN}
TTY_LOCAL_MODE_MUTANTS=	lost-local-high-bit bis-and bic-or set-retains-high \
			signed-get
TTY_LOCAL_MODE_UBSAN_MUTANT=	signed-shift
TTY_LAYOUT_CFLAGS=	-std=c17 -pedantic-errors -mcpu=cortex-m0plus \
			-mthumb -ffreestanding -fno-builtin -fno-common \
			-nostdinc -DKERNEL -I. -I${TOPSRC}/sys \
			-I${TOPSRC}/sys/arch -I${PORT_INCLUDE} ${GATE_WARN}
TTY_LAYOUT_BAD_OUTPUT=	tty_layout_contract_bad.out
MEM_SPAN_SRC=		${TOPSRC}/sys/dev/mem.c
MEM_SPAN_MACHDEP_SRC=	${TOPSRC}/sys/arch/rp2040/rp2040/machdep.c
MEM_SPAN_FUNCTIONS=	mem_kernel_span_functions.c
MEM_SPAN_DRIVER_MUTANTS=	endpoint-admission user-rejected kernel-rejected
MEM_SPAN_FUNCTION_MUTANTS=	kernel-first-byte
MEM_SPAN_MUTANTS=	${MEM_SPAN_DRIVER_MUTANTS} ${MEM_SPAN_FUNCTION_MUTANTS}
MEM_SPAN_CFLAGS=	-ffunction-sections -fdata-sections
MEM_SPAN_GATE_CFLAGS=	${GATE_CFLAGS} -Wno-pointer-to-int-cast
MEM_SPAN_LDFLAGS=	${FILE_OWNER_LDFLAGS}
MEM_SPAN_HEADER_DEPS=	${FIONREAD_HEADER_DEPS} ${USERCOPY_HEADER} \
			${TOPSRC}/sys/sys/user.h
PTRACE_SRC=		${TOPSRC}/sys/kern/sys_process.c
PTRACE_FUNCTIONS=	ptrace_functions.c
PTRACE_HEADER_DEPS=	${TOPSRC}/sys/sys/param.h \
			${TOPSRC}/sys/sys/systm.h ${TOPSRC}/sys/sys/user.h \
			${TOPSRC}/sys/sys/proc.h ${TOPSRC}/sys/sys/ptrace.h \
			${PORT_INCLUDE}/frame.h ${USER_CONTEXT_HEADER}
PTRACE_MUTANTS=		negative-trace short-read partial-read-publication \
			misaligned-u-offset u-upper-endpoint arbitrary-u-write \
			pc-misalignment sp-misalignment psr-control-overwrite \
			continue-early-pc signal-upper-endpoint step-fallthrough
PTRACE_ASM_SRC=	${TOPSRC}/lib/libc/arm/sys/ptrace.S
PTRACE_ASM_HEADER_DEPS=	${TOPSRC}/lib/libc/arm/sys/SYS.h \
			${TOPSRC}/include/syscall.h
PTRACE_ASM_FLAGS=	-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
			-I${TOPSRC}/include -I${TOPSRC}/lib/libc/arm/sys
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
	fionread_test swap_ioctl_test tty_local_mode_test mem_span_test \
	ptrace_test

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

${MEM_SPAN_FUNCTIONS}: mem_span_extract.py ${MEM_SPAN_MACHDEP_SRC}
	${PYTHON} mem_span_extract.py --source ${MEM_SPAN_MACHDEP_SRC} \
	    --output $@

${PTRACE_FUNCTIONS}: ptrace_extract.py ${PTRACE_SRC}
	${PYTHON} ptrace_extract.py --source ${PTRACE_SRC} --output $@

ptrace_test: ptrace_test.c ${PTRACE_FUNCTIONS} ${PTRACE_HEADER_DEPS} \
    hostkern_kern.c hostkern.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} -o $@ ptrace_test.c hostkern_kern.c \
	    hostkern.o

ptrace_test32: ptrace_test.c ${PTRACE_FUNCTIONS} ${PTRACE_HEADER_DEPS} \
    hostkern_kern.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} -DHK_ILP32 -o $@ \
	    ptrace_test.c hostkern_kern.c hostkern32.o

ptrace_test_sanitized: ptrace_test.c ${PTRACE_FUNCTIONS} \
    ${PTRACE_HEADER_DEPS} hostkern_kern.c hostkern.c hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o ptrace_hostkern_sanitized.o hostkern.c
	${HOST_CC} ${GATE_CFLAGS} ${KERNEL_SANITIZER_FLAGS} -o $@ \
	    ptrace_test.c hostkern_kern.c ptrace_hostkern_sanitized.o

check-ptrace-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-ptrace-sanitizer: ptrace_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.ptrace.sanitized -- \
	    ./ptrace_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.ptrace.sanitized \
	    "ptrace contracts: sanitizer runtime unavailable"
.endif

.for mutation in ${PTRACE_MUTANTS}
ptrace_${mutation}.mutant.c: ptrace_mutate.py ${PTRACE_FUNCTIONS}
	${PYTHON} ptrace_mutate.py --source ${PTRACE_FUNCTIONS} --output $@ \
	    --mutation ${mutation}

ptrace_${mutation}.mutant.test: ptrace_${mutation}.mutant.c ptrace_test.c \
    ${PTRACE_HEADER_DEPS} hostkern_kern.c hostkern.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} \
	    -DPTRACE_FUNCTIONS='"ptrace_${mutation}.mutant.c"' -o $@ \
	    ptrace_test.c hostkern_kern.c hostkern.o
.endfor

check-ptrace-mutants: \
    ${PTRACE_MUTANTS:@mutation@ptrace_${mutation}.mutant.test@}
	@set -eu; for mutation in ${PTRACE_MUTANTS}; do \
		output=ptrace_$$mutation.mutant.out; \
		if ./ptrace_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		negative-trace) expected='(parent.p_flag & P_TRACED) == 0' ;; \
		short-read) expected='ipc.ip_data == 0x12345678' ;; \
		partial-read-publication) expected='ipc.ip_data == 0x55667788' ;; \
		misaligned-u-offset) expected='ipc.ip_req == -1' ;; \
		u-upper-endpoint) expected='ptrace_user_word_offset_valid(USIZE) == 0' ;; \
		arbitrary-u-write) expected='u.u_uid == 17' ;; \
		pc-misalignment) expected='ipc.ip_req == -1' ;; \
		sp-misalignment) expected='ipc.ip_req == -1' ;; \
		psr-control-overwrite) expected='frame->tf_psr == UINT32_C(0xa100023f)' ;; \
		continue-early-pc) expected='frame->tf_pc == USER_LOWER + 2' ;; \
		signal-upper-endpoint) expected='ipc.ip_req == -1' ;; \
		step-fallthrough) expected='ipc.ip_req == -1' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL ptrace $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS ptrace $$mutation mutation rejected"; \
	done

ptrace_syscall.o: ${PTRACE_ASM_SRC} ${PTRACE_ASM_HEADER_DEPS}
	${GCCPREFIX}-gcc ${PTRACE_ASM_FLAGS} -c -o $@ ${PTRACE_ASM_SRC}

ptrace_syscall_clobber.S: ptrace_abi_mutate.py ${PTRACE_ASM_SRC}
	${PYTHON} ptrace_abi_mutate.py --source ${PTRACE_ASM_SRC} --output $@ \
	    --mutation drop-save-restore

ptrace_syscall_clobber.o: ptrace_syscall_clobber.S \
    ${PTRACE_ASM_HEADER_DEPS}
	${GCCPREFIX}-gcc ${PTRACE_ASM_FLAGS} -c -o $@ ptrace_syscall_clobber.S

ptrace_syscall_postrestore_clobber.S: ptrace_abi_mutate.py ${PTRACE_ASM_SRC}
	${PYTHON} ptrace_abi_mutate.py --source ${PTRACE_ASM_SRC} --output $@ \
	    --mutation postrestore-clobber

ptrace_syscall_postrestore_clobber.o: ptrace_syscall_postrestore_clobber.S \
    ${PTRACE_ASM_HEADER_DEPS}
	${GCCPREFIX}-gcc ${PTRACE_ASM_FLAGS} -c -o $@ \
	    ptrace_syscall_postrestore_clobber.S

check-ptrace-libc-abi: ptrace_syscall.o ptrace_syscall_clobber.o \
    ptrace_syscall_postrestore_clobber.o
	${PYTHON} ptrace_abi_check.py --objdump ${GCCPREFIX}-objdump \
	    --object ptrace_syscall.o
	@set -eu; for mutation in clobber postrestore_clobber; do \
		output=ptrace_syscall_$$mutation.out; \
		if ${PYTHON} ptrace_abi_check.py --objdump ${GCCPREFIX}-objdump \
		    --object ptrace_syscall_$$mutation.o \
		    >$$output 2>&1; then status=0; else status=$$?; fi; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F 'missing ordered instruction pattern' \
		    $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL ptrace libc $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS ptrace libc $$mutation mutation rejected"; \
	done

mem_span_kernel.o: ${MEM_SPAN_SRC} ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${KERN_CFLAGS} ${MEM_SPAN_CFLAGS} -c -o $@ ${MEM_SPAN_SRC}

mem_span_test: mem_span_test.c ${MEM_SPAN_FUNCTIONS} mem_span_kernel.o \
    hostkern.o ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${MEM_SPAN_LDFLAGS} -o $@ \
	    mem_span_test.c mem_span_kernel.o hostkern.o

mem_span_kernel32.o: ${MEM_SPAN_SRC} ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${KERN_CFLAGS} ${MEM_SPAN_CFLAGS} ${ILP32} \
	    -c -o $@ ${MEM_SPAN_SRC}

mem_span_test32: mem_span_test.c ${MEM_SPAN_FUNCTIONS} \
    mem_span_kernel32.o hostkern32.o ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${ILP32} ${MEM_SPAN_LDFLAGS} -o $@ \
	    mem_span_test.c mem_span_kernel32.o hostkern32.o

.for mutation in ${MEM_SPAN_DRIVER_MUTANTS}
mem_span_${mutation}.mutant.c: mem_span_mutate.py ${MEM_SPAN_SRC}
	${PYTHON} mem_span_mutate.py --source ${MEM_SPAN_SRC} --output $@ \
	    --mutation ${mutation}

mem_span_${mutation}.mutant.o: mem_span_${mutation}.mutant.c \
    ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${KERN_CFLAGS} ${MEM_SPAN_CFLAGS} -c -o $@ \
	    mem_span_${mutation}.mutant.c

mem_span_${mutation}.mutant.test: mem_span_test.c ${MEM_SPAN_FUNCTIONS} \
    mem_span_${mutation}.mutant.o hostkern.o ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${MEM_SPAN_LDFLAGS} -o $@ \
	    mem_span_test.c mem_span_${mutation}.mutant.o hostkern.o
.endfor

mem_span_kernel-first-byte.mutant.c: mem_span_mutate.py ${MEM_SPAN_FUNCTIONS}
	${PYTHON} mem_span_mutate.py --source ${MEM_SPAN_FUNCTIONS} --output $@ \
	    --mutation kernel-first-byte

mem_span_kernel-first-byte.mutant.test: mem_span_test.c \
    mem_span_kernel-first-byte.mutant.c mem_span_kernel.o hostkern.o \
    ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${MEM_SPAN_LDFLAGS} \
	    -DMEM_KERNEL_SPAN_FUNCTIONS='"mem_span_kernel-first-byte.mutant.c"' \
	    -o $@ mem_span_test.c mem_span_kernel.o hostkern.o

mem_span_endpoint-admission.mutant32.o: \
    mem_span_endpoint-admission.mutant.c ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${KERN_CFLAGS} ${MEM_SPAN_CFLAGS} ${ILP32} -c -o $@ \
	    mem_span_endpoint-admission.mutant.c

mem_span_endpoint-admission.mutant32.test: mem_span_test.c \
    ${MEM_SPAN_FUNCTIONS} mem_span_endpoint-admission.mutant32.o \
    hostkern32.o ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${ILP32} ${MEM_SPAN_LDFLAGS} -o $@ \
	    mem_span_test.c mem_span_endpoint-admission.mutant32.o hostkern32.o

check-mem-span-mutants: \
    ${MEM_SPAN_MUTANTS:@mutation@mem_span_${mutation}.mutant.test@}
	@set -eu; for mutation in ${MEM_SPAN_MUTANTS}; do \
		output=mem_span_$$mutation.mutant.out; \
		if ./mem_span_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		if [ "$$status" -ne 1 ]; then \
			cat $$output >&2; \
			echo "FAIL memory-device $$mutation mutation returned $$status" >&2; \
			exit 1; \
		fi; \
		echo "PASS memory-device $$mutation mutation rejected"; \
	done

mem_span_kernel_sanitized.o: ${MEM_SPAN_SRC} ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${KERN_CFLAGS} ${MEM_SPAN_CFLAGS} \
	    ${KERNEL_SANITIZER_FLAGS} -c -o $@ ${MEM_SPAN_SRC}

mem_span_hostkern_sanitized.o: hostkern.c hostkern.h
	${HOST_CC} ${HARNESS_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -c -o $@ hostkern.c

mem_span_test_sanitized: mem_span_test.c ${MEM_SPAN_FUNCTIONS} \
    mem_span_kernel_sanitized.o mem_span_hostkern_sanitized.o \
    ${MEM_SPAN_HEADER_DEPS}
	${HOST_CC} ${MEM_SPAN_GATE_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    ${MEM_SPAN_LDFLAGS} -o $@ mem_span_test.c \
	    mem_span_kernel_sanitized.o mem_span_hostkern_sanitized.o

check-mem-span-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-mem-span-sanitizer: mem_span_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.mem-span.sanitized -- \
	    ./mem_span_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.mem-span.sanitized \
	    "memory-device span contracts: sanitizer runtime unavailable"
.endif

${TTY_LOCAL_MODE_FUNCTIONS}: tty_local_mode_extract.py ${TTY_SRC}
	${PYTHON} tty_local_mode_extract.py --source ${TTY_SRC} --output $@

tty_local_mode_test: tty_local_mode_test.c ${TTY_LOCAL_MODE_FUNCTIONS}
	${HOST_CC} ${TTY_LOCAL_MODE_CFLAGS} -o $@ tty_local_mode_test.c

tty_local_mode_test32: tty_local_mode_test.c ${TTY_LOCAL_MODE_FUNCTIONS}
	${HOST_CC} ${TTY_LOCAL_MODE_CFLAGS} ${ILP32} -o $@ tty_local_mode_test.c

tty_local_mode_test_sanitized: tty_local_mode_test.c \
    ${TTY_LOCAL_MODE_FUNCTIONS}
	${HOST_CC} ${TTY_LOCAL_MODE_CFLAGS} ${KERNEL_SANITIZER_FLAGS} \
	    -o $@ tty_local_mode_test.c

.for mutation in ${TTY_LOCAL_MODE_MUTANTS} ${TTY_LOCAL_MODE_UBSAN_MUTANT}
tty_local_mode_${mutation}.mutant.c: tty_local_mode_mutate.py \
    ${TTY_LOCAL_MODE_FUNCTIONS}
	${PYTHON} tty_local_mode_mutate.py --source ${TTY_LOCAL_MODE_FUNCTIONS} \
	    --output $@ --mutation ${mutation}
.endfor

.for mutation in ${TTY_LOCAL_MODE_MUTANTS}
tty_local_mode_${mutation}.mutant.test: tty_local_mode_test.c \
    tty_local_mode_${mutation}.mutant.c
	${HOST_CC} ${TTY_LOCAL_MODE_CFLAGS} \
	    -DTTY_LOCAL_MODE_FUNCTIONS='"tty_local_mode_${mutation}.mutant.c"' \
	    -o $@ tty_local_mode_test.c
.endfor

tty_local_mode_${TTY_LOCAL_MODE_UBSAN_MUTANT}.mutant.test: \
    tty_local_mode_test.c \
    tty_local_mode_${TTY_LOCAL_MODE_UBSAN_MUTANT}.mutant.c
	${HOST_CC} ${TTY_LOCAL_MODE_CFLAGS} ${ILP32} -fsanitize=undefined \
	    -fno-sanitize-recover=all \
	    -DTTY_LOCAL_MODE_FUNCTIONS='"tty_local_mode_${TTY_LOCAL_MODE_UBSAN_MUTANT}.mutant.c"' \
	    -o $@ tty_local_mode_test.c

check-tty-local-mode-mutants: check-tty-local-mode-ubsan-oracle \
    ${TTY_LOCAL_MODE_MUTANTS:@mutation@tty_local_mode_${mutation}.mutant.test@}
	@set -eu; for mutation in ${TTY_LOCAL_MODE_MUTANTS}; do \
		output=tty_local_mode_$$mutation.mutant.out; \
		if ./tty_local_mode_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		if [ "$$status" -ne 1 ]; then \
			cat $$output >&2; \
			echo "FAIL tty local-mode $$mutation mutation returned $$status" >&2; \
			exit 1; \
		fi; \
		echo "PASS tty local-mode $$mutation mutation rejected"; \
	done

check-tty-local-mode-ubsan-oracle: tty_local_mode_ubsan_mutant_check.sh \
    tty_local_mode_ubsan_mutant_check_test.sh
	sh ./tty_local_mode_ubsan_mutant_check_test.sh \
	    ${.CURDIR}/tty_local_mode_ubsan_mutant_check.sh

check-tty-local-mode-ilp32-mutant: check-tty-local-mode-ubsan-oracle
.if ${TTY_LOCAL_MODE_ILP32_UBSAN_OK} == "yes"
check-tty-local-mode-ilp32-mutant: \
    tty_local_mode_${TTY_LOCAL_MODE_UBSAN_MUTANT}.mutant.test \
    tty_local_mode_ubsan_mutant_check.sh
	${TEST_EXECUTION} run kernel.tty-local-mode.signed-shift-negative -- \
	    sh ./tty_local_mode_ubsan_mutant_check.sh \
	    ./tty_local_mode_${TTY_LOCAL_MODE_UBSAN_MUTANT}.mutant.test
.else
	${TEST_EXECUTION} skip kernel.tty-local-mode.signed-shift-negative \
	    "tty local-mode signed-shift mutation: ILP32 sanitizer runtime unavailable"
.endif

check-tty-local-mode-sanitizer:
.if ${KERNEL_SANITIZER_OK} == "yes"
check-tty-local-mode-sanitizer: tty_local_mode_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.tty-local-mode.sanitized -- \
	    ./tty_local_mode_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.tty-local-mode.sanitized \
	    "tty local-mode contracts: sanitizer runtime unavailable"
.endif

tty-layout-contract-force:

tty_layout_contract.o: tty-layout-contract-force tty_layout_contract.c \
    ${MACHINE_STAMP}
	${GCCPREFIX}-gcc ${TTY_LAYOUT_CFLAGS} -MMD -MP \
	    -MF tty_layout_contract.d -c -o $@ tty_layout_contract.c

.sinclude "tty_layout_contract.d"

check-tty-layout-cross: tty_layout_contract.o
	@set -eu; \
	if ${GCCPREFIX}-gcc ${TTY_LAYOUT_CFLAGS} \
	    -DTTY_LAYOUT_EXPECTED_SIZE=121 -fsyntax-only \
	    tty_layout_contract.c >${TTY_LAYOUT_BAD_OUTPUT} 2>&1; then \
		cat ${TTY_LAYOUT_BAD_OUTPUT} >&2; \
		echo "FAIL tty layout accepted a 121-byte structure contract" >&2; \
		exit 1; \
	fi; \
	if ! grep -F "struct tty size changed" \
	    ${TTY_LAYOUT_BAD_OUTPUT} >/dev/null; then \
		cat ${TTY_LAYOUT_BAD_OUTPUT} >&2; \
		echo "FAIL tty layout rejection missed the size assertion" >&2; \
		exit 1; \
	fi; \
	echo "PASS tty layout rejects a 121-byte structure contract"

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
			direct-output-pointer) expected='provider_data != unaligned' ;; \
			unconditional-output-publication) expected='u.u_error == EIO && provider_calls == 1' ;; \
			short-output-copy) expected='copyout_bytes == cases[index].nbytes' ;; \
			omitted-output-publication) expected='copyin_calls == 0 && copyout_calls == 1' ;; \
			corrupt-output-initialization) expected='(unsigned char)unaligned[byte_index] == 0' ;; \
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

futimes_kernel32.o: ${FUTIMES_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} ${FUTIMES_CFLAGS} \
	    ${FUTIMES_RENAMES} -c -o $@ \
	    ${FUTIMES_SRC}

futimes_test32: futimes_test.c futimes_kernel32.o hostkern32.o hostkern.h \
    ${MACHINE_STAMP}
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} ${FUTIMES_RENAMES} \
	    ${FUTIMES_LDFLAGS} -o $@ \
	    futimes_test.c futimes_kernel32.o hostkern32.o

.for mutation in ${FUTIMES_MUTANTS}
futimes_${mutation}.mutant.c: futimes_mutate.py ${FUTIMES_SRC}
	${PYTHON} futimes_mutate.py --source ${FUTIMES_SRC} --output $@ \
	    --mutation ${mutation}

futimes_${mutation}.mutant.test: futimes_${mutation}.mutant.c \
    futimes_test.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${ILP32} ${FUTIMES_CFLAGS} \
	    ${FUTIMES_RENAMES} -c -o futimes_${mutation}.mutant.o \
	    futimes_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${ILP32} ${FUTIMES_RENAMES} \
	    ${FUTIMES_LDFLAGS} -o $@ futimes_test.c \
	    futimes_${mutation}.mutant.o hostkern32.o
.endfor

openat_namei32.o: ${OPENAT_NAMEI_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_CFLAGS} ${ILP32} -c -o $@ \
	    ${OPENAT_NAMEI_SRC}

openat_namei_multifs32.o: ${OPENAT_NAMEI_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_MULTIFS_CFLAGS} ${ILP32} -c -o $@ \
	    ${OPENAT_NAMEI_SRC}

openat_vnops32.o: ${OPENAT_VNOPS_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_CFLAGS} ${OPENAT_RENAMES} ${ILP32} \
	    -c -o $@ ${OPENAT_VNOPS_SRC}

openat_syscalls32.o: ${OPENAT_SYSCALL_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_CFLAGS} ${OPENAT_SYSCALL_RENAMES} ${ILP32} \
	    -c -o $@ ${OPENAT_SYSCALL_SRC}

openat_namei_test32: openat_test.c openat_namei32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    -DOPENAT_NAMEI_CONTRACT ${OPENAT_LDFLAGS} -o $@ openat_test.c \
	    openat_namei32.o hostkern32.o

openat_namei_multifs_test32: openat_test.c openat_namei_multifs32.o \
    hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    -DOPENAT_NAMEI_CONTRACT ${OPENAT_LDFLAGS} -o $@ openat_test.c \
	    openat_namei_multifs32.o hostkern32.o

openat_test32: openat_test.c openat_vnops32.o openat_syscalls32.o \
    ufs_namespace_kernel32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ openat_test.c openat_vnops32.o \
	    openat_syscalls32.o ufs_namespace_kernel32.o hostkern32.o

check-openat-ilp32: openat_namei_test32 openat_namei_multifs_test32 \
    openat_test32
	${TEST_EXECUTION} run kernel.openat-namei.ilp32 -- ./openat_namei_test32
	${TEST_EXECUTION} run kernel.openat-namei-multifs.ilp32 -- \
	    ./openat_namei_multifs_test32
	${TEST_EXECUTION} run kernel.openat.ilp32 -- ./openat_test32

unlinkat_beneath_namei32.o: ${UNLINKAT_BENEATH_NAMEI_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UNLINKAT_BENEATH_CFLAGS} ${ILP32} \
	    -c -o $@ ${UNLINKAT_BENEATH_NAMEI_SRC}

unlinkat_beneath_functions.c: unlinkat_beneath_extract.py \
    ${UNLINKAT_BENEATH_SYSCALL_SRC}
	${PYTHON} unlinkat_beneath_extract.py \
	    --source ${UNLINKAT_BENEATH_SYSCALL_SRC} --output $@

unlinkat_beneath_syscalls32.o: unlinkat_beneath_functions.c ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UNLINKAT_BENEATH_CFLAGS} ${ILP32} \
	    ${UNLINKAT_BENEATH_RENAMES} -c -o $@ \
	    unlinkat_beneath_functions.c

unlinkat_beneath_test32: unlinkat_beneath_test.c \
    unlinkat_beneath_namei32.o unlinkat_beneath_syscalls32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${UNLINKAT_BENEATH_RENAMES} ${OPENAT_LDFLAGS} -o $@ \
	    unlinkat_beneath_test.c unlinkat_beneath_namei32.o \
	    unlinkat_beneath_syscalls32.o hostkern32.o

check-unlinkat-beneath-ilp32: unlinkat_beneath_test32
	${TEST_EXECUTION} run kernel.unlinkat-beneath.ilp32 -- \
	    ./unlinkat_beneath_test32

.for mutation in ${UNLINKAT_BENEATH_MUTANTS}
unlinkat_beneath_${mutation}.mutant.c: unlinkat_beneath_mutate.py \
    unlinkat_beneath_extract.py ${UNLINKAT_BENEATH_SYSCALL_SRC}
	${PYTHON} unlinkat_beneath_mutate.py \
	    --source ${UNLINKAT_BENEATH_SYSCALL_SRC} --output $@ \
	    --mutation ${mutation}

unlinkat_beneath_${mutation}.mutant.test: \
    unlinkat_beneath_${mutation}.mutant.c unlinkat_beneath_test.c \
    unlinkat_beneath_namei32.o hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UNLINKAT_BENEATH_CFLAGS} ${ILP32} \
	    ${UNLINKAT_BENEATH_RENAMES} -c -o \
	    unlinkat_beneath_${mutation}.mutant.o \
	    unlinkat_beneath_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${UNLINKAT_BENEATH_RENAMES} ${OPENAT_LDFLAGS} -o $@ \
	    unlinkat_beneath_test.c unlinkat_beneath_namei32.o \
	    unlinkat_beneath_${mutation}.mutant.o hostkern32.o
.endfor

check-unlinkat-beneath-mutants: \
    ${UNLINKAT_BENEATH_MUTANTS:@mutation@unlinkat_beneath_${mutation}.mutant.test@}
	@set -eu; for mutation in ${UNLINKAT_BENEATH_MUTANTS}; do \
		output=unlinkat_beneath_$$mutation.mutant.out; \
		if ./unlinkat_beneath_$$mutation.mutant.test \
		    >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		omit-namespace-acquisition) expected='namespace_is_owned()' ;; \
		release-before-removal) expected='namespace_is_owned()' ;; \
		skip-identity-comparison) expected='u.u_error == ESTALE' ;; \
		drop-expected-reference) expected='expected_inode.i_count == 1' ;; \
		decrement-on-write-error) expected='expected_inode.i_nlink == 2' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL unlinkat_beneath $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS unlinkat_beneath $$mutation mutation rejected"; \
	done

publishat_beneath_functions.c: publishat_beneath_extract.py \
    unlinkat_beneath_extract.py ${PUBLISHAT_BENEATH_SYSCALL_SRC}
	${PYTHON} publishat_beneath_extract.py \
	    --source ${PUBLISHAT_BENEATH_SYSCALL_SRC} --output $@

publishat_beneath_syscalls32.o: publishat_beneath_functions.c \
    ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${PUBLISHAT_BENEATH_CFLAGS} ${ILP32} \
	    -c -o $@ publishat_beneath_functions.c

publishat_beneath_test32: publishat_beneath_test.c \
    publishat_beneath_syscalls32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ publishat_beneath_test.c \
	    publishat_beneath_syscalls32.o hostkern32.o

publishat_beneath_namei_test32: publishat_beneath_namei_test.c \
    unlinkat_beneath_namei32.o publishat_beneath_syscalls32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ publishat_beneath_namei_test.c \
	    unlinkat_beneath_namei32.o publishat_beneath_syscalls32.o \
	    hostkern32.o

check-publishat-beneath-ilp32: publishat_beneath_test32 \
    publishat_beneath_namei_test32
	${TEST_EXECUTION} run kernel.publishat-beneath.ilp32 -- \
	    ./publishat_beneath_test32
	${TEST_EXECUTION} run kernel.publishat-beneath-namei.ilp32 -- \
	    ./publishat_beneath_namei_test32

.for mutation in ${PUBLISHAT_BENEATH_MUTANTS}
publishat_beneath_${mutation}.mutant.c: publishat_beneath_mutate.py \
    publishat_beneath_extract.py unlinkat_beneath_extract.py \
    ${PUBLISHAT_BENEATH_SYSCALL_SRC}
	${PYTHON} publishat_beneath_mutate.py \
	    --source ${PUBLISHAT_BENEATH_SYSCALL_SRC} --output $@ \
	    --mutation ${mutation}

publishat_beneath_${mutation}.mutant.test: \
    publishat_beneath_${mutation}.mutant.c publishat_beneath_test.c \
    hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${PUBLISHAT_BENEATH_CFLAGS} ${ILP32} \
	    -c -o publishat_beneath_${mutation}.mutant.o \
	    publishat_beneath_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ publishat_beneath_test.c \
	    publishat_beneath_${mutation}.mutant.o hostkern32.o
.endfor

.for mutation in ${PUBLISHAT_NAMEI_MUTANTS}
publishat_beneath_${mutation}.mutant.c: dirremove_mutate.py \
    ${UNLINKAT_BENEATH_NAMEI_SRC}
	${PYTHON} dirremove_mutate.py --source ${UNLINKAT_BENEATH_NAMEI_SRC} \
	    --output $@ --mutation ${mutation}

publishat_beneath_${mutation}.mutant.test: \
    publishat_beneath_${mutation}.mutant.c \
    publishat_beneath_namei_test.c publishat_beneath_syscalls32.o \
    hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${PUBLISHAT_BENEATH_CFLAGS} ${ILP32} \
	    -c -o publishat_beneath_${mutation}.mutant.o \
	    publishat_beneath_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ publishat_beneath_namei_test.c \
	    publishat_beneath_${mutation}.mutant.o \
	    publishat_beneath_syscalls32.o hostkern32.o
.endfor

check-publishat-beneath-mutants: \
    ${PUBLISHAT_BENEATH_MUTANTS:@mutation@publishat_beneath_${mutation}.mutant.test@} \
    ${PUBLISHAT_NAMEI_MUTANTS:@mutation@publishat_beneath_${mutation}.mutant.test@}
	@set -eu; for mutation in ${PUBLISHAT_BENEATH_MUTANTS}; do \
		output=publishat_beneath_$$mutation.mutant.out; \
		if ./publishat_beneath_$$mutation.mutant.test \
		    >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		discard-reserve-write-error) expected='u.u_error == EIO' ;; \
		skip-staged-identity) expected='u.u_error == ESTALE' ;; \
		skip-staged-sticky) expected='u.u_error == EPERM' ;; \
		skip-target-identity) expected='u.u_error == ESTALE' ;; \
		use-at-fdcwd) expected='root_descriptor == ROOT_DESCRIPTOR' ;; \
		release-before-publication) expected='namespace_owned == 1' ;; \
		skip-same-parent) expected='name_data->ni_pdir == &root_inode' ;; \
		publish-after-remove-error) expected='enter_calls == 0' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL publishat_beneath $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS publishat_beneath $$mutation mutation rejected"; \
	done
	@set -eu; for mutation in ${PUBLISHAT_NAMEI_MUTANTS}; do \
		output=publishat_beneath_$$mutation.mutant.out; \
		if ./publishat_beneath_$$mutation.mutant.test \
		    >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		discard-enter-write-error) expected='u.u_error == EIO' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL publishat_beneath $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS publishat_beneath $$mutation mutation rejected"; \
	done

dirremove_kernel32.o: ${DIRREMOVE_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${DIRREMOVE_CFLAGS} ${ILP32} -c -o $@ \
	    ${DIRREMOVE_SRC}

dirremove_test32: dirremove_test.c dirremove_kernel32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ dirremove_test.c dirremove_kernel32.o \
	    hostkern32.o

check-dirremove-ilp32: dirremove_test32
	${TEST_EXECUTION} run kernel.dirremove.ilp32 -- ./dirremove_test32

.for mutation in ${DIRREMOVE_MUTANTS}
dirremove_${mutation}.mutant.c: dirremove_mutate.py ${DIRREMOVE_SRC}
	${PYTHON} dirremove_mutate.py --source ${DIRREMOVE_SRC} --output $@ \
	    --mutation ${mutation}

dirremove_${mutation}.mutant.test: dirremove_${mutation}.mutant.c \
    dirremove_test.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${DIRREMOVE_CFLAGS} ${ILP32} -c -o \
	    dirremove_${mutation}.mutant.o dirremove_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ dirremove_test.c \
	    dirremove_${mutation}.mutant.o hostkern32.o
.endfor

check-dirremove-mutants: \
    ${DIRREMOVE_MUTANTS:@mutation@dirremove_${mutation}.mutant.test@}
	@set -eu; for mutation in ${DIRREMOVE_MUTANTS}; do \
		output=dirremove_$$mutation.mutant.out; \
		if ./dirremove_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		discard-first-write-error) expected='dirremove(&name) == 0' ;; \
		discard-coalesced-write-error) expected='u.u_error == EIO' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL dirremove $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS dirremove $$mutation mutation rejected"; \
	done

ufs_namespace_kernel32.o: ${UFS_NAMESPACE_SRC} ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_NAMESPACE_CFLAGS} ${ILP32} -c -o $@ \
	    ${UFS_NAMESPACE_RENAMES} ${UFS_NAMESPACE_SRC}

ufs_namespace_test32: ufs_namespace_test.c ufs_namespace_kernel32.o \
    hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ ufs_namespace_test.c \
	    ufs_namespace_kernel32.o hostkern32.o

check-ufs-namespace-ilp32: ufs_namespace_test32
	${TEST_EXECUTION} run kernel.ufs-namespace.ilp32 -- \
	    ./ufs_namespace_test32

.for mutation in ${UFS_NAMESPACE_MUTANTS}
ufs_namespace_${mutation}.mutant.c: ufs_namespace_mutate.py \
    ${UFS_NAMESPACE_SRC}
	${PYTHON} ufs_namespace_mutate.py --source ${UFS_NAMESPACE_SRC} \
	    --output $@ --mutation ${mutation}

ufs_namespace_${mutation}.mutant.test: \
    ufs_namespace_${mutation}.mutant.c ufs_namespace_test.c hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_NAMESPACE_CFLAGS} \
	    ${UFS_NAMESPACE_RENAMES} ${ILP32} -c -o \
	    ufs_namespace_${mutation}.mutant.o \
	    ufs_namespace_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ ufs_namespace_test.c \
	    ufs_namespace_${mutation}.mutant.o hostkern32.o
.endfor

check-ufs-namespace-mutants: \
    ${UFS_NAMESPACE_MUTANTS:@mutation@ufs_namespace_${mutation}.mutant.test@}
	@set -eu; for mutation in ${UFS_NAMESPACE_MUTANTS}; do \
		output=ufs_namespace_$$mutation.mutant.out; \
		if ./ufs_namespace_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		omit-acquisition) expected='FAIL unexpected panic: ufs namespace gate' ;; \
		omit-wakeup) expected='wakeup_calls == 1' ;; \
		clear-on-interrupt) expected='sleep_calls == 2' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL UFS namespace $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS UFS namespace $$mutation mutation rejected"; \
	done

ufs_topology_functions.c: ufs_topology_extract.py ${UFS_TOPOLOGY_SRC}
	${PYTHON} ufs_topology_extract.py --source ${UFS_TOPOLOGY_SRC} \
	    --output $@

ufs_topology_syscalls32.o: ufs_topology_functions.c ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_TOPOLOGY_CFLAGS} \
	    ${UFS_TOPOLOGY_RENAMES} ${ILP32} -c -o $@ \
	    ufs_topology_functions.c

ufs_topology_test32: ufs_topology_test.c ufs_topology_syscalls32.o \
    ufs_namespace_kernel32.o hostkern32.o
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ ufs_topology_test.c \
	    ufs_topology_syscalls32.o ufs_namespace_kernel32.o hostkern32.o

check-ufs-topology-ilp32: ufs_topology_test32
	${TEST_EXECUTION} run kernel.ufs-topology.ilp32 -- ./ufs_topology_test32

.for mutation in ${UFS_TOPOLOGY_MUTANTS}
ufs_topology_${mutation}.mutant.c: ufs_topology_mutate.py \
    ufs_topology_extract.py ${UFS_TOPOLOGY_SRC}
	${PYTHON} ufs_topology_mutate.py --source ${UFS_TOPOLOGY_SRC} \
	    --output $@ --mutation ${mutation}

ufs_topology_${mutation}.mutant.test: \
    ufs_topology_${mutation}.mutant.c ufs_topology_test.c \
    ufs_namespace_kernel32.o hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${UFS_TOPOLOGY_CFLAGS} \
	    ${UFS_TOPOLOGY_RENAMES} ${ILP32} -c -o \
	    ufs_topology_${mutation}.mutant.o \
	    ufs_topology_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ ufs_topology_test.c \
	    ufs_topology_${mutation}.mutant.o ufs_namespace_kernel32.o \
	    hostkern32.o
.endfor

check-ufs-topology-mutants: \
    ${UFS_TOPOLOGY_MUTANTS:@mutation@ufs_topology_${mutation}.mutant.test@}
	@set -eu; for mutation in ${UFS_TOPOLOGY_MUTANTS}; do \
		output=ufs_topology_$$mutation.mutant.out; \
		if ./ufs_topology_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		omit-rename-acquisition|release-before-source-lookup|\
		release-before-directory-checkpath) \
			expected='error == EINTR' ;; \
		allow-directory-link|allow-directory-unlink) \
			expected='u.u_error == EPERM' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL UFS topology $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS UFS topology $$mutation mutation rejected"; \
	done

.for mutation in ${OPENAT_NAMEI_MUTANTS}
openat_${mutation}.mutant.c: openat_mutate.py ${OPENAT_NAMEI_SRC}
	${PYTHON} openat_mutate.py --source ${OPENAT_NAMEI_SRC} --output $@ \
	    --mutation ${mutation}

openat_${mutation}.mutant.test: openat_${mutation}.mutant.c \
    openat_test.c hostkern32.o hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_MULTIFS_CFLAGS} ${ILP32} \
	    -c -o openat_${mutation}.mutant.o openat_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_MULTIFS_DEFS} ${ILP32} \
	    -DOPENAT_NAMEI_CONTRACT ${OPENAT_LDFLAGS} -o $@ openat_test.c \
	    openat_${mutation}.mutant.o hostkern32.o
.endfor

.for mutation in ${OPENAT_VNOPS_MUTANTS}
openat_${mutation}.mutant.c: openat_mutate.py ${OPENAT_VNOPS_SRC}
	${PYTHON} openat_mutate.py --source ${OPENAT_VNOPS_SRC} --output $@ \
	    --mutation ${mutation}

openat_${mutation}.mutant.test: openat_${mutation}.mutant.c \
    openat_test.c openat_syscalls32.o ufs_namespace_kernel32.o hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_CFLAGS} ${OPENAT_RENAMES} ${ILP32} \
	    -c -o openat_${mutation}.mutant.o openat_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ openat_test.c \
	    openat_${mutation}.mutant.o openat_syscalls32.o \
	    ufs_namespace_kernel32.o hostkern32.o
.endfor

.for mutation in ${OPENAT_SYSCALL_MUTANTS}
openat_${mutation}.mutant.c: openat_syscall_mutate.py ${OPENAT_SYSCALL_SRC}
	${PYTHON} openat_syscall_mutate.py --source ${OPENAT_SYSCALL_SRC} \
	    --output $@ --mutation ${mutation}

openat_${mutation}.mutant.test: openat_${mutation}.mutant.c \
    openat_test.c openat_vnops32.o ufs_namespace_kernel32.o hostkern32.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${OPENAT_CFLAGS} \
	    ${OPENAT_SYSCALL_RENAMES} ${ILP32} -c -o \
	    openat_${mutation}.mutant.o openat_${mutation}.mutant.c
	${HOST_CC} ${GATE_CFLAGS} ${OPENAT_DEFS} ${ILP32} \
	    ${OPENAT_LDFLAGS} -o $@ openat_test.c openat_vnops32.o \
	    openat_${mutation}.mutant.o ufs_namespace_kernel32.o hostkern32.o
.endfor

check-openat-mutants: \
    ${OPENAT_MUTANTS:@mutation@openat_${mutation}.mutant.test@}
	@set -eu; for mutation in ${OPENAT_MUTANTS}; do \
		output=openat_$$mutation.mutant.out; \
		if ./openat_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		cwd-instead-of-dirfd) expected='result == &descriptor_inode' ;; \
		absolute-consults-dirfd) expected='result == &root_inode' ;; \
		accept-empty-beneath) expected='namei_at(&name, 7) == NULL' ;; \
		follow-beneath-symlink) expected='u.u_error == ELOOP' ;; \
		cross-beneath-mount) expected='u.u_error == EXDEV' ;; \
		follow-final-symlink) expected='u.u_error == ELOOP' ;; \
		hold-namespace-during-open-provider) \
			expected='!namespace_is_owned()' ;; \
		omit-rooted-open-policy|omit-rooted-mkdir-policy) \
			expected='expected_namei_policy' ;; \
		release-before-open|release-before-mkdir-publication|\
		release-competing-open-owner) expected='namespace_is_owned()' ;; \
		omit-rooted-open-release|omit-rooted-mkdir-release) \
			expected='!namespace_is_owned()' ;; \
		omit-mkdir-parent-rollback) \
			expected='mkdir_parent_inode.i_nlink == 2' ;; \
		omit-mkdir-parent-iput) \
			expected='mkdir_parent_inode.i_count == 0' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL openat $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS openat $$mutation mutation rejected"; \
	done

check-futimes-mutants: \
    ${FUTIMES_MUTANTS:@mutation@futimes_${mutation}.mutant.test@}
	@set -eu; for mutation in ${FUTIMES_MUTANTS}; do \
		output=futimes_$$mutation.mutant.out; \
		if ./futimes_$$mutation.mutant.test >$$output 2>&1; then \
			status=0; \
		else \
			status=$$?; \
		fi; \
		case $$mutation in \
		short-copy) expected='length == sizeof(struct timeval) * 2' ;; \
		descriptor-before-copy) expected='descriptor_calls == 0' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL futimes $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS futimes $$mutation mutation rejected"; \
	done

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

.for mutation in ${SIGRETURN_MUTANTS}
sigreturn_context_${mutation}.mutant.c: sigreturn_context_extract.py \
    ${SIGRETURN_SRC}
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for sigreturn extraction" >&2; \
		exit 2; \
	fi
	${PYTHON} sigreturn_context_extract.py --source ${SIGRETURN_SRC} \
	    --output $@ --mutation ${mutation}

sigreturn_context_${mutation}.mutant.test: sigreturn_context_test.c \
    sigreturn_context_${mutation}.mutant.c ${SIGRETURN_HEADER_DEPS}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -DSIGRETURN_CONTEXT_FUNCTION='"sigreturn_context_${mutation}.mutant.c"' \
	    -o $@ sigreturn_context_test.c
.endfor

sigreturn_context_test32: sigreturn_context_test.c \
    sigreturn_context_function.c ${SIGRETURN_HEADER_DEPS}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    -o $@ sigreturn_context_test.c

sigreturn_context_test_sanitized: sigreturn_context_test.c \
    sigreturn_context_function.c ${SIGRETURN_HEADER_DEPS}
	${HOST_CC} ${SYSCALL_GATE_CFLAGS} ${ILP32} \
	    ${KERNEL_SANITIZER_FLAGS} \
	    -o $@ sigreturn_context_test.c

check-sigreturn-mutants: \
    ${SIGRETURN_MUTANTS:@mutation@sigreturn_context_${mutation}.mutant.test@}
	@set -eu; for mutation in ${SIGRETURN_MUTANTS}; do \
		output=sigreturn_context_$$mutation.mutant.out; \
		if ./sigreturn_context_$$mutation.mutant.test \
		    >$$output 2>&1; then status=0; else status=$$?; fi; \
		case $$mutation in \
		direct-user) expected='exact context uses one complete copy' ;; \
		reread-user) expected='sigreturn restores low registers' ;; \
		pc-admission) expected='odd PC reports EINVAL' ;; \
		sp-alignment) expected='misaligned SP reports EINVAL' ;; \
		frame-width) expected='short base frame reports EINVAL' ;; \
		padding-width) expected='missing alignment padding reports EINVAL' ;; \
		psr-policy) expected='missing Thumb bit reports EINVAL' ;; \
		early-publication) expected='rejection preserves signal mask' ;; \
		esac; \
		if [ "$$status" -ne 1 ] || \
		    ! grep -F "$$expected" $$output >/dev/null; then \
			cat $$output >&2; \
			echo "FAIL sigreturn $$mutation mutation rejection" >&2; \
			exit 1; \
		fi; \
		echo "PASS sigreturn $$mutation mutation rejected"; \
	done

check-sigreturn-sanitizer:
.if ${SIGRETURN_SANITIZER_OK} == "yes"
check-sigreturn-sanitizer: sigreturn_context_test_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run kernel.sigreturn-context.sanitized -- \
	    ./sigreturn_context_test_sanitized
.else
	${TEST_EXECUTION} skip kernel.sigreturn-context.sanitized \
	    "sigreturn context: ILP32 sanitizer runtime unavailable"
.endif

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
	    swap_ioctl_test32 tty_local_mode_test32 mem_span_test32 \
	    ptrace_test32 \
	    mem_span_endpoint-admission.mutant32.test \
	    check-tty-local-mode-ilp32-mutant \
	    usercopy_span_test32 usercopy_span_endpoint_mutant syscall_args_test32 \
	    syscall_args_dispatch_mutant sigreturn_context_test32 \
	    check-sigreturn-mutants \
	    exec_elf_admission_test32 exec_elf_segment_span_mutant \
	    exec_elf_read_failure_mutant exec_finish_signal_mutant \
	    exec_elf_surrounding_contract_test32 exec_stack_geometry_mutant \
	    exec_elf_text_ownership_mutant \
	    sysctl_test_compact sysctl_test_wide \
	    ialloc_test ialloc_test_pico ialloc_test_diagnostic \
	    balloc_test balloc_test_pico \
	    ufs_explicit_time_test ufs_explicit_time_bad futimes_test32 \
	    check-futimes-mutants check-openat-ilp32 check-openat-mutants \
	    check-unlinkat-beneath-ilp32 check-unlinkat-beneath-mutants \
	    check-publishat-beneath-ilp32 check-publishat-beneath-mutants \
	    check-dirremove-ilp32 check-dirremove-mutants \
	    check-ufs-namespace-ilp32 check-ufs-namespace-mutants \
	    check-ufs-topology-ilp32 check-ufs-topology-mutants \
	    capacity_test \
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
	${TEST_EXECUTION} run kernel.tty-local-mode.ilp32 -- \
	    ./tty_local_mode_test32
	${TEST_EXECUTION} run kernel.mem-span.ilp32 -- ./mem_span_test32
	${TEST_EXECUTION} run kernel.ptrace.ilp32 -- ./ptrace_test32
	@if ./mem_span_endpoint-admission.mutant32.test \
	    >mem_span_endpoint-admission.mutant32.out 2>&1; then status=0; \
	else status=$$?; fi; \
	if [ "$$status" -ne 1 ] || \
	    ! grep -F "discontiguous_error == EFAULT" \
	    mem_span_endpoint-admission.mutant32.out >/dev/null || \
	    ! grep -F "wrapped_error == EFAULT" \
	    mem_span_endpoint-admission.mutant32.out >/dev/null; then \
		cat mem_span_endpoint-admission.mutant32.out >&2; \
		echo "FAIL memory-device ILP32 endpoint mutation" >&2; \
		exit 1; \
	fi; \
	echo "PASS memory-device ILP32 endpoint mutation rejected"
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
	${TEST_EXECUTION} run kernel.futimes.ilp32 -- ./futimes_test32
	${TEST_EXECUTION} run kernel.capacity.ilp32 -- ./capacity_test
.else
	${TEST_EXECUTION} skip kernel.printf.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.signal-auth.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.rwuio.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.file-owner.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.fionread.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.swap-ioctl.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.tty-local-mode.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.tty-local-mode.signed-shift-negative "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.mem-span.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.ptrace.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
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
	${TEST_EXECUTION} skip kernel.futimes.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.openat-namei.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.openat-namei-multifs.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.openat.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.unlinkat-beneath.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.dirremove.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.ufs-namespace.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.ufs-topology.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip kernel.capacity.ilp32 "check-kernel-ilp32: ${HOST_CC} cannot compile and execute ILP32 binaries"
.endif

check: ${PROGS} ${SHUTDOWN_CHECKS} check-credential-sanitizer \
    check-rwuio-mutants check-rwuio-sanitizer \
    check-file-owner-mutants check-file-owner-sanitizer \
    check-file-status-mutants check-fionread-mutants \
    check-fionread-sanitizer check-swap-ioctl-mutants \
    check-swap-ioctl-sanitizer check-tty-local-mode-mutants \
    check-tty-local-mode-sanitizer check-mem-span-mutants \
    check-mem-span-sanitizer check-ptrace-mutants check-ptrace-sanitizer \
    check-sigreturn-sanitizer
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
	rm -f ${PTRACE_FUNCTIONS} ptrace_test32 ptrace_test_sanitized \
	    ptrace_hostkern_sanitized.o
	rm -f ptrace_*.mutant.c ptrace_*.mutant.test ptrace_*.mutant.out
	rm -f ptrace_syscall.o ptrace_syscall_clobber.S \
	    ptrace_syscall_clobber.o ptrace_syscall_clobber.out \
	    ptrace_syscall_postrestore_clobber.S \
	    ptrace_syscall_postrestore_clobber.o \
	    ptrace_syscall_postrestore_clobber.out
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
	rm -f tty_local_mode_test tty_local_mode_test32 tty_layout_contract.o \
	    tty_layout_contract.d ${TTY_LAYOUT_BAD_OUTPUT} \
	    tty_local_mode_test_sanitized ${TTY_LOCAL_MODE_FUNCTIONS}
	rm -f tty_local_mode_*.mutant.c tty_local_mode_*.mutant.test \
	    tty_local_mode_*.mutant.out
	rm -f ${MEM_SPAN_FUNCTIONS} mem_span_kernel.o mem_span_kernel32.o \
	    mem_span_test32 mem_span_test_sanitized mem_span_kernel_sanitized.o \
	    mem_span_hostkern_sanitized.o
	rm -f mem_span_*.mutant.c mem_span_*.mutant.o \
	    mem_span_*.mutant.test mem_span_*.mutant.out \
	    mem_span_*.mutant32.o mem_span_*.mutant32.test \
	    mem_span_*.mutant32.out
	rm -f usercopy_functions.c usercopy_endpoint_mutant.c
	rm -f usercopy_span_test32 usercopy_span_endpoint_mutant
	rm -f syscall_args_block.c syscall_args_dispatch_mutant.c
	rm -f syscall_args_test32 syscall_args_dispatch_mutant
	rm -f sigreturn_context_function.c sigreturn_context_test32 \
	    sigreturn_context_test_sanitized
	rm -f sigreturn_context_*.mutant.c sigreturn_context_*.mutant.test \
	    sigreturn_context_*.mutant.out
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
	rm -f futimes_kernel32.o futimes_test32
	rm -f futimes_*.mutant.c futimes_*.mutant.o futimes_*.mutant.test \
	    futimes_*.mutant.out
	rm -f openat_namei32.o openat_namei_multifs32.o openat_vnops32.o \
	    openat_syscalls32.o openat_namei_test32 \
	    openat_namei_multifs_test32 openat_test32
	rm -f openat_*.mutant.c openat_*.mutant.o openat_*.mutant.test \
	    openat_*.mutant.out
	rm -f unlinkat_beneath_functions.c unlinkat_beneath_namei32.o \
	    unlinkat_beneath_syscalls32.o unlinkat_beneath_test32
	rm -f unlinkat_beneath_*.mutant.c unlinkat_beneath_*.mutant.o \
	    unlinkat_beneath_*.mutant.test unlinkat_beneath_*.mutant.out
	rm -f publishat_beneath_functions.c publishat_beneath_syscalls32.o \
	    publishat_beneath_test32 publishat_beneath_namei_test32
	rm -f publishat_beneath_*.mutant.c publishat_beneath_*.mutant.o \
	    publishat_beneath_*.mutant.test publishat_beneath_*.mutant.out
	rm -f dirremove_kernel32.o dirremove_test32
	rm -f dirremove_*.mutant.c dirremove_*.mutant.o \
	    dirremove_*.mutant.test dirremove_*.mutant.out
	rm -f ufs_namespace_kernel32.o ufs_namespace_test32
	rm -f ufs_namespace_*.mutant.c ufs_namespace_*.mutant.o \
	    ufs_namespace_*.mutant.test ufs_namespace_*.mutant.out
	rm -f ufs_topology_functions.c ufs_topology_syscalls32.o \
	    ufs_topology_test32
	rm -f ufs_topology_*.mutant.c ufs_topology_*.mutant.o \
	    ufs_topology_*.mutant.test ufs_topology_*.mutant.out
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
	check-fionread-mutants check-fionread-sanitizer \
	check-ptrace-mutants check-ptrace-sanitizer check-ptrace-libc-abi \
	check-futimes-mutants check-openat-ilp32 check-openat-mutants \
	check-unlinkat-beneath-ilp32 check-unlinkat-beneath-mutants \
	check-publishat-beneath-ilp32 check-publishat-beneath-mutants \
	check-dirremove-ilp32 check-dirremove-mutants \
	check-ufs-namespace-ilp32 check-ufs-namespace-mutants \
	check-ufs-topology-ilp32 check-ufs-topology-mutants \
	check-tty-local-mode-mutants check-tty-local-mode-ubsan-oracle \
	check-tty-local-mode-ilp32-mutant \
	check-tty-local-mode-sanitizer check-tty-layout-cross \
	tty-layout-contract-force clean
