TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the tier takes the
# tree's default from share/mk/sys.mk, as tests/kernel and tests/libc_sysctl
# do; a MACHINE on the command line still wins over both.
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
PROG=	umount_contract_test
TARGET_SRC=	${TOPSRC}/sbin/umount/umount.c

# The tree's own headers, because struct statfs, getmntinfo() and the MOUNT_*
# names are the ones sbin/umount compiles against and the host has no BSD
# equivalent for. include/sys/types.h reads "typedef u_int size_t", so the
# tier runs at ILP32, where the tree's size_t and the host's unsigned are the
# same four bytes. umount_shim.h carries the rest of that reasoning.
ILP32=	-m32

# include/sys/param.h reaches the port's headers as <machine/*.h>, a name the
# root Makefile spells as a symlink inside its build targets. A tier that has
# to run in an unconfigured tree cannot rely on that, so it generates one
# forwarding header per port header, as tests/kernel does.
MACHINE_STAMP=	machine/.stamp
PORT_INCLUDE=	${TOPSRC}/include/${MACHINE}
TREE=	-I. -I${TOPSRC}/include

# include/time.h declares strftime with the tree's own size_t, which the host
# compiler holds against its builtin; -fno-builtin is what the libc build
# itself uses.
WARN=	-Wall -Wextra -Werror
CFLAGS=	-std=gnu17 ${WARN} -fno-builtin ${ILP32}

# sbin/umount still defines functions the old way, which clang rejects by
# default and the arm-none-eabi gcc the program ships from does not; the gate
# excuses it rather than rewriting the program for a host compiler alone.
TARGET_WARN=	-Wno-deprecated-non-prototype -Wno-old-style-definition

# main() is the entry the gate calls rather than the one the loader calls,
# and exit() is what the gate catches so it can read the status instead of
# losing the process.
# printf and fprintf move aside because include/stdio.h spells stderr as
# &_iob[2], a FILE the host's libc does not own; the gate keeps the program's
# diagnostics rather than letting them reach a foreign stream.
RENAMES=	-Dmain=umount_main -Dexit=umount_exit \
		-Dprintf=umount_printf -Dfprintf=umount_fprintf \
		-include stdio.h -include umount_noreturn.h

ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/umprobe$$$$.c; \
		if ${HOSTCC} -m32 -o /dev/null /tmp/umprobe$$$$.c \
		    2>/dev/null; then echo yes; else echo no; fi; \
		rm -f /tmp/umprobe$$$$.c

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG}
	./${PROG}
.else
	@echo "umount_contracts: ${HOSTCC} cannot build 32-bit binaries; skipped" >&2
.endif

${MACHINE_STAMP}:
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/$$name>" > machine/$$name; \
	done
	@touch $@

# The shim carries no tree header, so it compiles without ${TREE} and reaches
# the host's <stdio.h> the way any host program does.
shim.o: umount_shim.c umount_shim.h
	${HOSTCC} -std=gnu17 ${WARN} ${ILP32} -c -o $@ umount_shim.c

umount.o: ${TARGET_SRC} umount_noreturn.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} ${TARGET_WARN} -c -o $@ \
	    ${TARGET_SRC}

gate.o: umount_contract_test.c umount_shim.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} -c -o $@ umount_contract_test.c

${PROG}: gate.o umount.o shim.o
	${HOSTCC} ${CFLAGS} -o $@ gate.o umount.o shim.o

clean:
	rm -f ${PROG} gate.o umount.o shim.o
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

all: check

.PHONY: all check clean
