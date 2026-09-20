# The on-device regression programs: each subdirectory builds one or more
# a.outs with the tree's cross toolchain after "bmake MACHINE=rp2040
# build". None is in the root manifest; stage one for a test image with a
# "file /usr/bin/<name>" line in distrib/rp2040/mi.rp2040, or copy it to
# the board over the console. The host-run checks (cache_footprint,
# divider_ownership, elf2aout_layout, exec_spool, flash_swap and
# uarea_exchange) are reached through the root Makefile's check targets.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BOARD_PROGS=	exec_hsaout fptest romprobe sigtest streamtest \
		swapram_epoch swapram_evacuate swapram_large tartest
BOARD_TARGETS=	${BOARD_PROGS:S/^/build-/}

all:		${BOARD_TARGETS}

.for program in ${BOARD_PROGS}
build-${program}:
		${MAKE} -C ${program} all
.endfor

clean:
		-for i in ${BOARD_PROGS}; do ${MAKE} -C $$i clean; done

.PHONY:		all clean ${BOARD_TARGETS}
