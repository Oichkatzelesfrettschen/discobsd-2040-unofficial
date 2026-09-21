PARAM = -DPICO
PARAM += -DRP2040
PARAM += -DUARTUSB_ENABLED
PARAM += -DUART_ENABLED
PARAM += -DEXEC_HSAOUT
PARAM += -DSWAPRAM_KB=16
PARAM += -DSWAPRAM
PARAM += -DUART_BAUD=115200
PARAM += -DCONS_MINOR=0
PARAM += -DCONS_MAJOR=UARTUSB_MAJOR
PARAM += -DCORE_DEFAULT=0
PARAM += -DLINEAR_NAME_CACHE
PARAM += -DLINEAR_BUFFER_CACHE
PARAM += -DCOMPACT_SWAPMAP
PARAM += -DCOMPACT_INODE_FIELDS
PARAM += -DLINEAR_INODE_CACHE
PARAM += -DSINGLE_UFS_ROOT
PARAM += -DNMOUNT=1
PARAM += -DNFL=2
PARAM += -DCAPACITY_STATS
PARAM += -DSYSTRACE
PARAM += -DHALTREBOOT
PARAM += -DBUS_KHZ=125000
PARAM += -DCPU_KHZ=125000
LDSCRIPT = "RP2040.ld"
DEBUG = -g
#
# Makefile for DiscoBSD/rp2040
#
# DEBUG is set to -g by config if debugging is requested (config -g).
#

# sources are located via $S relative to the compilation directory
S=		../../../..

_mach?=		rp2040
_arch?=		arm
_machdir?=	$S/arch/${_mach}
_archdir?=	$S/arch/${_arch}
_confdir?=	${_machdir}/conf

include		${_confdir}/compiler.mk

# arch/rp2040/dhara/compat supplies the three standard headers the vendored
# Dhara sources name and a -nostdinc kernel does not have.
INCLUDES=	-nostdinc -I. -I$S -I$S/arch -I${_archdir}/include
INCLUDES+=	-I${_machdir}/dhara/compat
# arch/rp2040/heatshrink holds the codec the SWAPRAM tier calls, and its
# compat directory the two standard headers dhara/compat does not carry.
INCLUDES+=	-I${_machdir}/heatshrink -I${_machdir}/heatshrink/compat
DEPFLAGS=	-MT $@ -MP -MD -MF .deps/$*.dep
DEFS=		${INCLUDES} ${PARAM} -DKERNEL ${DEPFLAGS}
CWARNFLAGS=	-Wall -Wextra -Werror

CMACHCPU=	-mcpu=cortex-m0plus
# DiscoBSD predates C23, where an empty parameter list means (void) rather
# than an unspecified one. GCC 15 and later default to C23 and reject the
# indirect syscall dispatch in sysctl.c, which reproduces on the untouched
# STM32 tree too. Pin the standard the sources were written against.
CSTDFLAGS=	-std=gnu17

CMACHFLAGS=	-ffreestanding -fno-builtin -fno-common
CMACHFLAGS+=	-mfloat-abi=soft
CMACHFLAGS+=	-mthumb -mabi=aapcs -mlittle-endian
CMACHFLAGS+=	${CMACHCPU}

COPTS?=		-O
CFLAGS=		${DEBUG} ${CWARNFLAGS} ${CSTDFLAGS} ${CMACHFLAGS} ${COPTS}
AFLAGS=		-x assembler-with-cpp ${CWARNFLAGS} ${CMACHFLAGS}
LDFLAGS=	-T ${_confdir}/${LDSCRIPT} -T ${_confdir}/kern.ldscript
LDFLAGS+=	-nostdlib -Map=$@.map -z noexecstack
LDFLAGS+=	--warn-rwx-segments --fatal-warnings
# Cortex-M0+ has no divide instruction, so the compiler emits calls to the
# libgcc helpers __aeabi_idiv and friends. The Cortex-M4 targets divide in
# hardware and never link against libgcc, which is why the STM32 Makefile
# leaves LDADD empty.
LIBGCC!=	${CC} ${CMACHFLAGS} -print-libgcc-file-name
LDADD=		${LIBGCC}

# compile rules: rules are named COMPILE_${SUFFIX}
# SUFFIX is the file suffix, capitalized (e.g. C for a .c file).

COMPILE_C=	${CC} ${CFLAGS} ${DEFS} -c $<
COMPILE_S=	${AS} ${AFLAGS} ${DEFS} -c $<

# PARAM reaches every object as -D flags, and .deps records only the headers
# an object read, so an option edited in Config and re-emitted by config
# left every existing object stale. ${PARAMSTAMP} holds the PARAM text and
# is rewritten at parse time only when that text differs, so its mtime
# moves exactly when an option changes and every object below depends on
# it. tools/verify_rp2040_swapram_link.sh checks the linked result.
PARAMSTAMP=	.params
_paramstamp!=	printf '%s\n' '${PARAM}' | cmp -s - ${PARAMSTAMP} 2>/dev/null || \
		printf '%s\n' '${PARAM}' > ${PARAMSTAMP}; echo ${PARAMSTAMP}

OBJS = exec_aout.o exec_conf.o exec_elf.o exec_script.o exec_subr.o \
	errlist.o init_main.o init_sysent.o kern_clock.o kern_descrip.o \
	kern_exec.o kern_exit.o kern_fork.o kern_mman.o kern_proc.o \
	kern_prot.o kern_prot2.o kern_resource.o kern_sig.o kern_sig2.o \
	kern_subr.o kern_synch.o kern_sysctl.o kern_time.o \
	subr_capacity.o subr_prf.o subr_rmap.o sys_generic.o sys_inode.o \
	sys_pipe.o sys_process.o syscalls.o tty.o tty_subr.o tty_tty.o \
	ufs_alloc.o ufs_bio.o ufs_bmap.o ufs_dsort.o ufs_fio.o \
	ufs_inode.o ufs_mount.o ufs_namei.o ufs_subr.o ufs_syscalls.o \
	ufs_syscalls2.o vfs_vnops.o vm_sched.o vm_swap.o vm_swp.o cons.o \
	mem.o swap.o clock.o conf.o fault.o machdep.o mpu.o \
	sig_machdep.o syscall.o sysctl.o systick.o uart.o usb.o flash.o \
	flash_swap.o error.o journal.o map.o swapram.o swapram_pool.o \
	heatshrink_encoder.o heatshrink_decoder.o subr_crc32.o \
	exec_hsaout.o hsx_stream.o hsx_decoder.o

CFILES = $S/kern/exec_aout.c $S/kern/exec_conf.c $S/kern/exec_elf.c \
	$S/kern/exec_script.c $S/kern/exec_subr.c $S/kern/errlist.c \
	$S/kern/init_main.c $S/kern/init_sysent.c $S/kern/kern_clock.c \
	$S/kern/kern_descrip.c $S/kern/kern_exec.c $S/kern/kern_exit.c \
	$S/kern/kern_fork.c $S/kern/kern_mman.c $S/kern/kern_proc.c \
	$S/kern/kern_prot.c $S/kern/kern_prot2.c $S/kern/kern_resource.c \
	$S/kern/kern_sig.c $S/kern/kern_sig2.c $S/kern/kern_subr.c \
	$S/kern/kern_synch.c $S/kern/kern_sysctl.c $S/kern/kern_time.c \
	$S/kern/subr_capacity.c $S/kern/subr_prf.c $S/kern/subr_rmap.c \
	$S/kern/sys_generic.c $S/kern/sys_inode.c $S/kern/sys_pipe.c \
	$S/kern/sys_process.c $S/kern/syscalls.c $S/kern/tty.c \
	$S/kern/tty_subr.c $S/kern/tty_tty.c $S/kern/ufs_alloc.c \
	$S/kern/ufs_bio.c $S/kern/ufs_bmap.c $S/kern/ufs_dsort.c \
	$S/kern/ufs_fio.c $S/kern/ufs_inode.c $S/kern/ufs_mount.c \
	$S/kern/ufs_namei.c $S/kern/ufs_subr.c $S/kern/ufs_syscalls.c \
	$S/kern/ufs_syscalls2.c $S/kern/vfs_vnops.c $S/kern/vm_sched.c \
	$S/kern/vm_swap.c $S/kern/vm_swp.c $S/dev/cons.c $S/dev/mem.c \
	$S/dev/swap.c $S/arch/rp2040/rp2040/clock.c \
	$S/arch/rp2040/rp2040/conf.c $S/arch/rp2040/rp2040/fault.c \
	$S/arch/rp2040/rp2040/machdep.c $S/arch/rp2040/rp2040/mpu.c \
	$S/arch/rp2040/rp2040/sig_machdep.c \
	$S/arch/rp2040/rp2040/syscall.c $S/arch/rp2040/rp2040/sysctl.c \
	$S/arch/rp2040/rp2040/systick.c $S/arch/rp2040/dev/uart.c \
	$S/arch/rp2040/dev/usb.c $S/arch/rp2040/dev/flash.c \
	$S/arch/rp2040/dev/flash_swap.c $S/arch/rp2040/dhara/error.c \
	$S/arch/rp2040/dhara/journal.c $S/arch/rp2040/dhara/map.c \
	$S/arch/rp2040/rp2040/swapram.c \
	$S/arch/rp2040/rp2040/swapram_pool.c \
	$S/arch/rp2040/heatshrink/heatshrink_encoder.c \
	$S/arch/rp2040/heatshrink/heatshrink_decoder.c \
	$S/kern/subr_crc32.c $S/arch/rp2040/rp2040/exec_hsaout.c \
	$S/arch/rp2040/rp2040/hsx_stream.c \
	$S/arch/rp2040/rp2040/hsx_decoder.c swapunix.c

# load lines for config "xxx" will be emitted as:
# xxx: ${SYSTEM_DEP} swapxxx.o
#	${SYSTEM_LD_HEAD}
#	${SYSTEM_LD} swapxxx.o
#	${SYSTEM_LD_TAIL}
SYSTEM_OBJ=	boot2.o locore0.o locore.o ${OBJS} ioconf.o
${SYSTEM_OBJ} swapunix.o: ${PARAMSTAMP}
SYSTEM_DEP=	Makefile ioconf.c .WAIT machine sys .deps ${SYSTEM_OBJ} \
		${_confdir}/${LDSCRIPT:S/"//g} ${_confdir}/kern.ldscript
# The link is one recipe of three lines, and each line is one && chain, so
# the status a step returns is the status of the line and a failed step
# stops the ones after it: a failed newvers.sh compiles no vers.c, a failed
# vers.o compile links nothing, and a failed conversion writes no later
# artifact. The chain starts by removing the kernel, vers.o and every
# derived image, so a failed invocation leaves no output of an older link
# in the place a new one was asked for. Every converter writes to a .tmp
# name that mv publishes on success, so a partial file is never found
# under a finished artifact's name. tests/build_failure injects a failure
# into each step and asserts the ones after it never run.
#
# picotool is the one optional tool, with three outcomes that the recipe
# keeps apart: absent, which "all" reports and tolerates because a host
# without it still builds a kernel to inspect; present and successful,
# which publishes $@.uf2; and present but failed, which fails the build.
# The .elf.uf2 rule below is the explicit request, and it admits no skip.
# picotool types a file by its extension, so the .tmp output names its
# type with -t uf2; picotool 2.3.1 rejects the name without it.
PICOTOOL?=	picotool
SYSTEM_LD_HEAD=	rm -f $@ $@.elf $@.hex $@.bin $@.dis $@.uf2 vers.o && \
		sh $S/conf/newvers.sh > vers.c.tmp && mv vers.c.tmp vers.c && \
		${CC} ${CFLAGS} -c vers.c
SYSTEM_LD=	@echo ${LD} ${LDFLAGS} '$${SYSTEM_OBJ}' vers.o -o $@ ${LDADD}; \
		${LD} ${LDFLAGS} ${SYSTEM_OBJ} vers.o -o $@ ${LDADD}
SYSTEM_LD_TAIL=	${SIZE} $@ && cp $@ $@.elf && \
		${OBJCOPY} -O ihex $@ $@.hex.tmp && mv $@.hex.tmp $@.hex && \
		${OBJCOPY} -O binary $@ $@.bin.tmp && mv $@.bin.tmp $@.bin && \
		${OBJDUMP} -d -S $@ > $@.dis.tmp && mv $@.dis.tmp $@.dis && \
		if command -v ${PICOTOOL} >/dev/null 2>&1; then \
		    ${PICOTOOL} uf2 convert --quiet $@.elf $@.uf2.tmp -t uf2 && \
		    mv $@.uf2.tmp $@.uf2; \
		else \
		    echo "${PICOTOOL} absent: $@.uf2 not written" >&2; \
		fi

.SUFFIXES:	.elf .uf2
.elf.uf2:
	${PICOTOOL} uf2 convert --quiet $< $@.tmp -t uf2 && mv $@.tmp $@

unix: ${SYSTEM_DEP} swapunix.o
	${SYSTEM_LD_HEAD}
	${SYSTEM_LD} swapunix.o
	${SYSTEM_LD_TAIL}

swapunix.o: swapunix.c
	${COMPILE_C}

all: unix

clean:
	rm -rf .deps unix *.elf *.o *.map *.dis *.bin *.hex *.uf2 *.tmp boot2.S machine sys \
	    vers.c .compileversion .oldversion ${PARAMSTAMP}

clean-all: clean
	rm -f *.h ioconf.c swap*.c vers.c

reconfig ioconf.c: Config $S/../tools/bin/config
	$S/../tools/bin/config Config
	${MAKE} clean

machine:
	ln -s ${_machdir}/include $@

sys:
	ln -s $S/sys $@

.deps:
	mkdir .deps

locore.o: ${_machdir}/${_mach}/locore.S
	${COMPILE_S}

locore0.o: ${_machdir}/${_mach}/locore0.S
	${COMPILE_S}

# The second stage the boot ROM runs from SRAM to bring up execute-in-place.
# It is linked on its own at the SRAM address the ROM copies it to, reduced
# to raw bytes, then padded to 256 bytes with the CRC32 trailer the ROM
# checks and assembled into the .boot2 section, which kern.ldscript places
# at flash offset 0. See boot2/boot2.ld and tools/boot2sum.
BOOT2SRC=	${_machdir}/boot2/boot2_w25q080.S
BOOT2LD=	${_machdir}/boot2/boot2.ld
BOOT2SUM=	$S/../tools/bin/boot2sum

boot2.elf: ${BOOT2SRC} ${BOOT2LD}
	${CC} ${AFLAGS} ${INCLUDES} -nostdlib -nostartfiles \
	    -Wl,-T,${BOOT2LD} -o $@ ${BOOT2SRC}

boot2.bin: boot2.elf
	${OBJCOPY} -O binary boot2.elf $@

boot2.S: boot2.bin ${BOOT2SUM}
	${BOOT2SUM} boot2.bin $@

boot2.o: boot2.S
	${COMPILE_S}

ioconf.o: ioconf.c
	${COMPILE_C}

exec_aout.o: $S/kern/exec_aout.c
	${COMPILE_C}

exec_conf.o: $S/kern/exec_conf.c
	${COMPILE_C}

exec_elf.o: $S/kern/exec_elf.c
	${COMPILE_C}

exec_script.o: $S/kern/exec_script.c
	${COMPILE_C}

exec_subr.o: $S/kern/exec_subr.c
	${COMPILE_C}

errlist.o: $S/kern/errlist.c
	${COMPILE_C}

init_main.o: $S/kern/init_main.c
	${COMPILE_C}

init_sysent.o: $S/kern/init_sysent.c
	${COMPILE_C}

kern_clock.o: $S/kern/kern_clock.c
	${COMPILE_C}

kern_descrip.o: $S/kern/kern_descrip.c
	${COMPILE_C}

kern_exec.o: $S/kern/kern_exec.c
	${COMPILE_C}

kern_exit.o: $S/kern/kern_exit.c
	${COMPILE_C}

kern_fork.o: $S/kern/kern_fork.c
	${COMPILE_C}

kern_mman.o: $S/kern/kern_mman.c
	${COMPILE_C}

kern_proc.o: $S/kern/kern_proc.c
	${COMPILE_C}

kern_prot.o: $S/kern/kern_prot.c
	${COMPILE_C}

kern_prot2.o: $S/kern/kern_prot2.c
	${COMPILE_C}

kern_resource.o: $S/kern/kern_resource.c
	${COMPILE_C}

kern_sig.o: $S/kern/kern_sig.c
	${COMPILE_C}

kern_sig2.o: $S/kern/kern_sig2.c
	${COMPILE_C}

kern_subr.o: $S/kern/kern_subr.c
	${COMPILE_C}

kern_synch.o: $S/kern/kern_synch.c
	${COMPILE_C}

kern_sysctl.o: $S/kern/kern_sysctl.c
	${COMPILE_C}

kern_time.o: $S/kern/kern_time.c
	${COMPILE_C}

subr_capacity.o: $S/kern/subr_capacity.c
	${COMPILE_C}

subr_prf.o: $S/kern/subr_prf.c
	${COMPILE_C}

subr_rmap.o: $S/kern/subr_rmap.c
	${COMPILE_C}

sys_generic.o: $S/kern/sys_generic.c
	${COMPILE_C}

sys_inode.o: $S/kern/sys_inode.c
	${COMPILE_C}

sys_pipe.o: $S/kern/sys_pipe.c
	${COMPILE_C}

sys_process.o: $S/kern/sys_process.c
	${COMPILE_C}

syscalls.o: $S/kern/syscalls.c
	${COMPILE_C}

tty.o: $S/kern/tty.c
	${COMPILE_C}

tty_subr.o: $S/kern/tty_subr.c
	${COMPILE_C}

tty_tty.o: $S/kern/tty_tty.c
	${COMPILE_C}

ufs_alloc.o: $S/kern/ufs_alloc.c
	${COMPILE_C}

ufs_bio.o: $S/kern/ufs_bio.c
	${COMPILE_C}

ufs_bmap.o: $S/kern/ufs_bmap.c
	${COMPILE_C}

ufs_dsort.o: $S/kern/ufs_dsort.c
	${COMPILE_C}

ufs_fio.o: $S/kern/ufs_fio.c
	${COMPILE_C}

ufs_inode.o: $S/kern/ufs_inode.c
	${COMPILE_C}

ufs_mount.o: $S/kern/ufs_mount.c
	${COMPILE_C}

ufs_namei.o: $S/kern/ufs_namei.c
	${COMPILE_C}

ufs_subr.o: $S/kern/ufs_subr.c
	${COMPILE_C}

ufs_syscalls.o: $S/kern/ufs_syscalls.c
	${COMPILE_C}

ufs_syscalls2.o: $S/kern/ufs_syscalls2.c
	${COMPILE_C}

vfs_vnops.o: $S/kern/vfs_vnops.c
	${COMPILE_C}

vm_sched.o: $S/kern/vm_sched.c
	${COMPILE_C}

vm_swap.o: $S/kern/vm_swap.c
	${COMPILE_C}

vm_swp.o: $S/kern/vm_swp.c
	${COMPILE_C}

cons.o: $S/dev/cons.c
	${COMPILE_C}

mem.o: $S/dev/mem.c
	${COMPILE_C}

swap.o: $S/dev/swap.c
	${COMPILE_C}

clock.o: $S/arch/rp2040/rp2040/clock.c
	${COMPILE_C}

conf.o: $S/arch/rp2040/rp2040/conf.c
	${COMPILE_C}

fault.o: $S/arch/rp2040/rp2040/fault.c
	${COMPILE_C}

machdep.o: $S/arch/rp2040/rp2040/machdep.c
	${COMPILE_C}

mpu.o: $S/arch/rp2040/rp2040/mpu.c
	${COMPILE_C}

sig_machdep.o: $S/arch/rp2040/rp2040/sig_machdep.c
	${COMPILE_C}

syscall.o: $S/arch/rp2040/rp2040/syscall.c
	${COMPILE_C}

sysctl.o: $S/arch/rp2040/rp2040/sysctl.c
	${COMPILE_C}

systick.o: $S/arch/rp2040/rp2040/systick.c
	${COMPILE_C}

uart.o: $S/arch/rp2040/dev/uart.c
	${COMPILE_C}

usb.o: $S/arch/rp2040/dev/usb.c
	${COMPILE_C}

flash.o: $S/arch/rp2040/dev/flash.c
	${COMPILE_C}

flash_swap.o: $S/arch/rp2040/dev/flash_swap.c
	${COMPILE_C}

error.o: $S/arch/rp2040/dhara/error.c
	${COMPILE_C}

journal.o: $S/arch/rp2040/dhara/journal.c
	${COMPILE_C}

map.o: $S/arch/rp2040/dhara/map.c
	${COMPILE_C}

swapram.o: $S/arch/rp2040/rp2040/swapram.c
	${COMPILE_C}

swapram_pool.o: $S/arch/rp2040/rp2040/swapram_pool.c
	${COMPILE_C}

heatshrink_encoder.o: $S/arch/rp2040/heatshrink/heatshrink_encoder.c
	${COMPILE_C}

heatshrink_decoder.o: $S/arch/rp2040/heatshrink/heatshrink_decoder.c
	${COMPILE_C}

subr_crc32.o: $S/kern/subr_crc32.c
	${COMPILE_C}

exec_hsaout.o: $S/arch/rp2040/rp2040/exec_hsaout.c
	${COMPILE_C}

hsx_stream.o: $S/arch/rp2040/rp2040/hsx_stream.c
	${COMPILE_C}

hsx_decoder.o: $S/arch/rp2040/rp2040/hsx_decoder.c
	${COMPILE_C}


-include .deps/*.dep
