# The on-device regression programs: each subdirectory builds one or more
# a.outs with the tree's cross toolchain after "bmake MACHINE=rp2040
# build". None is in the root manifest; stage one for a test image with a
# "file /usr/bin/<name>" line in distrib/rp2040/mi.rp2040, or copy it to
# the board over the console. The host-run checks (cache_footprint,
# divider_helpers, divider_ownership, elf2aout_layout, exec_spool,
# flash_swap and uarea_exchange) are reached through the root Makefile's
# check targets.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BOARD_PROGS=	divtest exec_hsaout fptest gpioprobe lcdexttest lcdgfxtest romprobe \
		sigtest \
		stack_chain \
		streamtest \
		stdio_capacity swapmaptest swapram_evacuate tartest tiledemo \
		user_window fork_refusal capacity_exhaustion preempt windemo
BOARD_TARGETS=	${BOARD_PROGS:S/^/build-/}

all:		${BOARD_TARGETS}

.for program in ${BOARD_PROGS}
build-${program}:
		${MAKE} -C ${program} all
.endfor

# The executables the board runs, installed in /usr/tests/rp2040 for the
# PicoCalc's SD root (distrib/rp2040/Makefile.inc, sdroot), which has the
# space the flash root lacks. The host-side drivers (*.py) stay here.
BOARD_BINS=	getconf/confcheck divtest/divtest exec_hsaout/textcrc fptest/fptest \
		lcdexttest/lcdexttest lcdgfxtest/lcdgfxtest romprobe/romprobe \
		sigtest/sigtest stack_chain/stackchain \
		streamtest/streamtest stdio_capacity/stdiocap \
		swapmaptest/swapmaptest swapram_evacuate/evactest \
		tartest/tartest tiledemo/tiledemo user_window/windowtest \
		fork_refusal/forktest capacity_exhaustion/clistbound \
		capacity_exhaustion/filetab capacity_exhaustion/inotab \
		capacity_exhaustion/proctab

install:	all
		mkdir -p ${DESTDIR}/usr/tests/rp2040
		for f in ${BOARD_BINS}; do \
			cp $$f ${DESTDIR}/usr/tests/rp2040/ || exit 1; done

clean:
		-for i in ${BOARD_PROGS}; do ${MAKE} -C $$i clean; done

.PHONY:		all install clean ${BOARD_TARGETS}
