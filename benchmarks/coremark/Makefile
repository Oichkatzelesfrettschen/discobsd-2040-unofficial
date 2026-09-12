# EEMBC CoreMark (Apache-2.0, github.com/eembc/coremark), ported to DiscoBSD.
#
# core_list_join.c, core_main.c, core_matrix.c, core_state.c, core_util.c and
# coremark.h are the six unmodifiable upstream core files (see coremark.md5,
# checked by `bmake check`); core_portme.c/core_portme.h are this port's own
# files, timed through gettimeofday(2) and sized through MEM_STACK -- see
# core_portme.c's own comments for why, and
# sys/arch/rp2040/doc/research/benchmarks.md for the run rules this follows.
TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		coremark

# The upstream run rule is >=10 seconds of timed iteration; 6000 iterations
# is the tree's starting estimate against the ~236-250 CoreMark (125 MHz,
# bare-metal -O3) reference figures in benchmarks.md, scaled down for a
# unix process's syscall and scheduling overhead against that bare-metal
# ceiling. Override per run, e.g. `bmake MACHINE=rp2040 ITERATIONS=20000`,
# once an on-device timing pass shows the real DiscoBSD iterations/sec.
ITERATIONS?=	6000

# Snapshot CFLAGS (sys.mk's arm-none-eabi-gcc flags) before appending our own
# -D's, so FLAGS_STR reports the actual compile line without CFLAGS
# referencing itself.
CM_BASE_CFLAGS:=${CFLAGS}
CFLAGS+=	-O2 -DITERATIONS=${ITERATIONS} \
		-DFLAGS_STR='"${CM_BASE_CFLAGS} -O2"'

OBJS=		core_list_join.o core_main.o core_matrix.o core_state.o \
		core_util.o core_portme.o

HOSTCC?=	cc
HOSTBIN=	${PROG}-host

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Host build: same six vendored core files and the same core_portme.c/h,
# compiled with the host's cc instead of arm-none-eabi-gcc, to prove the
# port file is wired correctly (validation CRCs, canonical score line)
# before the binary ever reaches hardware. See README.md's run rules for
# what "Correct operation validated" and the CoreMark 1.0 score line mean.
host:		core_list_join.c core_main.c core_matrix.c core_state.c \
		core_util.c core_portme.c coremark.h core_portme.h
		${HOSTCC} -std=c99 -O2 -Wall -Wextra -DITERATIONS=${ITERATIONS} \
			-DFLAGS_STR='"-O2"' -I${TOPSRC}/benchmarks/coremark \
			-o ${HOSTBIN} core_list_join.c core_main.c \
			core_matrix.c core_state.c core_util.c core_portme.c

run-host:	host
		./${HOSTBIN}

check:
		md5sum -c coremark.md5

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
