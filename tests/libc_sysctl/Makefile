TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the tier takes the
# tree's default from share/mk/sys.mk the way tests/kernel does; a MACHINE on
# the command line still wins over both.
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
PROG=	libc_sysctl_test
TARGET_DIR=	${TOPSRC}/lib/libc/gen

# The tree's own userland headers, so struct utsname and the CTL_* names are
# the ones the target compiles against. That makes the width of size_t part of
# the gate: include/sys/types.h reads "typedef u_int size_t", so the tier runs
# at ILP32, where the tree's size_t and the host's unsigned are the same four
# bytes and a length means the same thing on both sides of the link.
# libc_shim.h carries the rest of that reasoning.
ILP32=	-m32

# include/sys/param.h reaches the port's headers as <machine/*.h>, a name the
# root Makefile spells as a symlink inside its build targets. A tier that has
# to run in an unconfigured tree cannot rely on that, so it generates one
# forwarding header per port header instead, as tests/kernel does.
MACHINE_STAMP=	machine/.stamp
# Every port carries the headers this gate reaches, so which one it takes
# changes nothing it measures.
PORT_INCLUDE=	${TOPSRC}/include/${MACHINE}
TREE=	-I. -I${TOPSRC}/include

# include/time.h declares strftime with the tree's own size_t, which the host
# compiler holds against its builtin; -fno-builtin is what the libc build
# itself uses. errno moves aside because include/errno.h declares it as a
# plain int while the host's libc keeps it in thread-local storage, and the
# two cannot be linked together.
WARN=	-Wall -Wextra -Werror
CFLAGS=	-std=c99 ${WARN} -fno-builtin ${ILP32}
RENAMES=	-Dsysctl=test_sysctl -Derrno=test_errno

ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/lsprobe$$$$.c; \
		if ${HOSTCC} -m32 -o /dev/null /tmp/lsprobe$$$$.c \
		    2>/dev/null; then echo yes; else echo no; fi; \
		rm -f /tmp/lsprobe$$$$.c

# The version walk in uname() writes, so the run that proves it stays inside
# the structure is worth making under the sanitizers as well: the guard
# catches a write that leaves the object and the sanitizer names the byte.
SAN=		-fsanitize=address,undefined -fno-omit-frame-pointer
SAN_OK!=	printf 'int main(void){return 0;}\n' > /tmp/lssan$$$$.c; \
		if ${HOSTCC} -m32 -fsanitize=address,undefined \
		    -o /dev/null /tmp/lssan$$$$.c 2>/dev/null; \
		then echo yes; else echo no; fi; \
		rm -f /tmp/lssan$$$$.c

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG}
	./${PROG}
.if ${SAN_OK} == "yes"
	@${MAKE} ${PROG}_san
	./${PROG}_san
.else
	@echo "libc_sysctl: no 32-bit sanitizer runtime; that run skipped" >&2
.endif
.else
	@echo "libc_sysctl: ${HOSTCC} cannot build 32-bit binaries; skipped" >&2
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
shim.o: libc_shim.c libc_shim.h
	${HOSTCC} -std=c99 ${WARN} ${ILP32} -c -o $@ libc_shim.c

uname.o: ${TARGET_DIR}/uname.c ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} -c -o $@ ${TARGET_DIR}/uname.c

gethostname.o: ${TARGET_DIR}/gethostname.c ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} \
	    -Wno-deprecated-non-prototype -c -o $@ ${TARGET_DIR}/gethostname.c

gate.o: libc_sysctl_test.c libc_shim.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} -c -o $@ libc_sysctl_test.c

${PROG}: gate.o uname.o gethostname.o shim.o
	${HOSTCC} ${CFLAGS} -o $@ gate.o uname.o gethostname.o shim.o

shim_san.o: libc_shim.c libc_shim.h
	${HOSTCC} -std=c99 ${WARN} ${ILP32} ${SAN} -c -o $@ libc_shim.c

uname_san.o: ${TARGET_DIR}/uname.c ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${SAN} ${TREE} ${RENAMES} -c -o $@ \
	    ${TARGET_DIR}/uname.c

gethostname_san.o: ${TARGET_DIR}/gethostname.c ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${SAN} ${TREE} ${RENAMES} \
	    -Wno-deprecated-non-prototype -c -o $@ ${TARGET_DIR}/gethostname.c

gate_san.o: libc_sysctl_test.c libc_shim.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${SAN} ${TREE} ${RENAMES} -c -o $@ \
	    libc_sysctl_test.c

${PROG}_san: gate_san.o uname_san.o gethostname_san.o shim_san.o
	${HOSTCC} ${CFLAGS} ${SAN} -o $@ gate_san.o uname_san.o \
	    gethostname_san.o shim_san.o

clean:
	rm -f ${PROG} ${PROG}_san
	rm -f gate.o uname.o gethostname.o shim.o
	rm -f gate_san.o uname_san.o gethostname_san.o shim_san.o
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

all: check

.PHONY: all check clean
