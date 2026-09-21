TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
PROG=		dd_contract_test
TABLES=		dd_tables_test
HOST_DD=	dd_host
TARGET_SRC=	${TOPSRC}/bin/dd/dd.c

# The operands under test wrap where long and off_t are four bytes, which is
# what the RP2040 compiles to and what include/sys/types.h declares. A
# 64-bit run rejects the same operands for being large rather than for
# wrapping and so measures nothing, and its off_t reaches offsets the board
# cannot name; the gate runs at ILP32, as tests/umount_contracts does.
ILP32=		-m32

WARN=		-Wall -Wextra -Werror
CFLAGS=		-std=gnu17 ${WARN} ${ILP32} -O0

# The program reaches open(2), read(2) and lseek(2) for the paths under
# test, and a failing read is one of them, so the gate answers those three
# itself. main() is the entry the gate calls rather than the one the loader
# calls, and exit() is what the gate catches so an operand rejection is a
# verdict instead of the end of the run. dd_shim.h states the model the
# three answer with.
RENAMES=	-Dmain=dd_main -Dexit=dd_exit -Dopen=dd_open \
		-Dread=dd_read -Dlseek=dd_lseek \
		-I${.CURDIR} -include dd_shim.h

# The renames rewrite the declarations in <unistd.h>, <fcntl.h> and
# <stdlib.h>. glibc substitutes differently named fortified entry points for
# some of them once _FORTIFY_SOURCE is on, so the gate turns it off and
# builds at -O0, where the plain declarations are what the renames reach.
FORTIFY=	-U_FORTIFY_SOURCE

ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/ddprobe$$$$.c; \
		if ${HOSTCC} -m32 -o /dev/null /tmp/ddprobe$$$$.c \
		    2>/dev/null; then echo yes; else echo no; fi; \
		rm -f /tmp/ddprobe$$$$.c

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG} ${TABLES} ${HOST_DD}
	./${PROG}
	./${TABLES}
	sh ${.CURDIR}/filesystem_test.sh ${.CURDIR}/${HOST_DD}
.else
	@echo "dd_contracts: ${HOSTCC} cannot build 32-bit binaries; skipped" >&2
.endif

dd.o: ${TARGET_SRC} dd_shim.h
	${HOSTCC} ${CFLAGS} ${FORTIFY} ${RENAMES} -c -o $@ ${TARGET_SRC}

shim.o: dd_shim.c dd_shim.h
	${HOSTCC} ${CFLAGS} -c -o $@ ${.CURDIR}/dd_shim.c

gate.o: dd_contract_test.c dd_shim.h
	${HOSTCC} ${CFLAGS} -c -o $@ ${.CURDIR}/dd_contract_test.c

${PROG}: gate.o dd.o shim.o
	${HOSTCC} ${CFLAGS} -o $@ gate.o dd.o shim.o

# The conversion tables, read out of the shipped dd.o so that what the suite
# measures is the program's own decomposition rather than a copy of it. The
# test carries the 256-byte atoibm table dd used to hold and requires the
# four-exception form to reproduce it entry by entry.
tables.o: dd_tables_test.c
	${HOSTCC} ${CFLAGS} -c -o $@ ${.CURDIR}/dd_tables_test.c

${TABLES}: tables.o dd.o shim.o
	${HOSTCC} ${CFLAGS} -o $@ tables.o dd.o shim.o

# The program as it ships, for the seek and truncation contracts, which
# hold over a real file rather than over the gate's own descriptors.
${HOST_DD}: ${TARGET_SRC}
	${HOSTCC} ${CFLAGS} -o $@ ${TARGET_SRC}

clean:
	rm -f ${PROG} ${TABLES} ${HOST_DD} gate.o dd.o shim.o tables.o

all: check

.PHONY: all check clean
