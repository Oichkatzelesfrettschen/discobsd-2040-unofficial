TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
TIME_SRC?=	${TOPSRC}/usr.bin/time/time.c
MUTANT_COMPILERS?=	gcc clang
TARGET_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

# The time source and the dispatcher template of sbin/utilbox, linked
# against the host libc with a wait3 shim, run as "time" and as
# "utilbox time". The shipped a.out runs only on the board, so this tier
# measures the status logic and the dispatch, not the target libc.
check: check-behavior check-mutants

check-behavior:
	sh ${.CURDIR}/build.sh ${HOSTCC} ${TOPSRC} ${TIME_SRC} ${.CURDIR}/build
	sh ${.CURDIR}/check.sh ${.CURDIR}/build

check-mutants:
	sh ${.CURDIR}/mutate.sh ${TOPSRC} ${TIME_SRC} ${MUTANT_COMPILERS}

# The same source at the target's width and instruction set, which is the
# compile the shipped program takes.
check-cross: time_target.o

time_target.o: ${TIME_SRC}
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TIME_SRC}

clean:
	rm -rf build time_target.o

all: check

.PHONY: all check check-behavior check-mutants check-cross clean
