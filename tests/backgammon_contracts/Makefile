TOPSRC!=cd ../..; pwd
# bmake predefines MACHINE as the host's architecture, so the tier takes the
# tree's default from share/mk/sys.mk the way tests/libc_sysctl does; a
# MACHINE on the command line still wins over both.
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
PROG=	backgammon_contract_test
GAMESRC=	${TOPSRC}/games/backgammon

# bg_shim.h carries the reasoning for the two-translation-unit split and for
# the width: at ILP32 the tree's size_t and the host's unsigned agree.
ILP32=	-m32

# back.h reaches <sgtty.h>, which reaches <sys/ioctl.h> for struct sgttyb and
# struct tchars, and <sys/ttychars.h> for CERASE and CINTR. Those are the
# tree's, so the constants under test are the ones the target compiles
# against. include/sys/param.h names the port's headers as <machine/*.h>,
# which the root Makefile spells as a symlink inside its build targets; a tier
# that has to run in an unconfigured tree generates one forwarding header per
# port header instead.
MACHINE_STAMP=	machine/.stamp
PORT_INCLUDE=	${TOPSRC}/include/${MACHINE}
TREE=	-I. -I${.CURDIR} -I${GAMESRC} -I${TOPSRC}/include

WARN=	-Wall -Wextra -Werror
# -nostdinc keeps the checking side on the tree's declarations alone, the way
# the cross compile reaches them, and leaves the builtin knowledge of exit()
# in place so that _Noreturn on getout() is checked rather than disbelieved:
# include/stdlib.h declares exit() without _Noreturn, and -fno-builtin would
# make every path through getout() read as a return.
CFLAGS=	-std=gnu17 ${WARN} -nostdinc ${ILP32}

# subs.c carries K&R definitions throughout. clang names that class
# -Wdeprecated-non-prototype and gcc -Wold-style-definition, and gcc accepts
# an unrecognized -Wno- option in silence, so both spellings ride along.
LEGACY=	-Wno-deprecated-non-prototype -Wno-old-style-definition

ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG}
	${TEST_EXECUTION} run backgammon.ilp32 -- ./${PROG}
.else
	${TEST_EXECUTION} skip backgammon.ilp32 "backgammon_contracts: ${HOSTCC} cannot compile and execute ILP32 binaries"
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
bg_shim.o: ${.CURDIR}/bg_shim.c ${.CURDIR}/bg_shim.h
	${HOSTCC} -std=gnu17 ${WARN} ${ILP32} -c -o $@ ${.CURDIR}/bg_shim.c

subs.o: ${GAMESRC}/subs.c ${GAMESRC}/back.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${LEGACY} ${TREE} -c -o $@ ${GAMESRC}/subs.c

gate.o: ${.CURDIR}/backgammon_contract_test.c ${.CURDIR}/bg_shim.h \
	    ${GAMESRC}/back.h ${MACHINE_STAMP}
	${HOSTCC} ${CFLAGS} ${TREE} -c -o $@ \
	    ${.CURDIR}/backgammon_contract_test.c

${PROG}: gate.o subs.o bg_shim.o
	${HOSTCC} ${CFLAGS} -o $@ gate.o subs.o bg_shim.o

# The same source at the target's width and instruction set, which is the
# compile the game takes, held to the full warning set that games/backgammon
# now builds under.
check-cross: subs_target.o

subs_target.o: ${GAMESRC}/subs.c ${GAMESRC}/back.h
	${GCCPREFIX}-gcc ${WARN} -Werror -std=gnu17 -fno-common \
	    -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	    -mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os \
	    -c -o $@ ${GAMESRC}/subs.c

clean:
	rm -f ${PROG} gate.o subs.o bg_shim.o subs_target.o
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

all: check

.PHONY: all check check-cross clean
