# The system call wrappers of the 64-bit view of time (sys/_time_t.h,
# lib/libc/gen/time64_*.c) executed as Thumb-1 under qemu-arm.
#
# time64_test.c is compiled with _TIME_BITS=64 against the tree's headers,
# so its calls bind to the __NAME64 wrappers; narrow_calls.c is compiled
# with the default view and stands in for the 32-bit entry points the
# wrappers call. No kernel runs: the gate establishes the conversions of
# the wrappers, compiled by the cross compiler for the target instruction
# set, and nothing about the system calls behind them.
#
# TIME64_SRC names the directory holding time64.h and the members, and
# PREFIX the names of the products; check.sh sets both to build each
# mutated copy, which the test must reject. REQUIRE_QEMU makes a missing
# qemu-arm an error; otherwise the target prints SKIP and no ARM execution
# has occurred.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

TIME64_SRC?=	${TOPSRC}/lib/libc/gen
PREFIX?=	good
MEMBERS!=	cd ${TOPSRC}/lib/libc/gen && ls time64_*.c
OBJS=		${MEMBERS:S,^,${PREFIX}_,:.c=.o} ${PREFIX}_test.o ${PREFIX}_narrow.o
# The headers that carry the two views; an object is rebuilt when one moves.
VIEW_HEADERS=	${TOPSRC}/sys/sys/_time_t.h ${TOPSRC}/sys/sys/time.h \
		${TOPSRC}/sys/sys/stat.h ${TOPSRC}/sys/sys/resource.h \
		${TOPSRC}/sys/sys/select.h ${TOPSRC}/sys/sys/wait.h
TREE_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -Os ${QEMU_ARM_CPUFLAGS} \
		-ffreestanding -fno-common -nostdinc -I${TOPSRC}/include

all: check

.for member in ${MEMBERS}
${PREFIX}_${member:.c=.o}: ${TIME64_SRC}/${member} ${TIME64_SRC}/time64.h \
    ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -I${TIME64_SRC} -c -o $@ \
	    ${TIME64_SRC}/${member}
.endfor

${PREFIX}_test.o: time64_test.c narrow_calls.h ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -c -o $@ time64_test.c

${PREFIX}_narrow.o: narrow_calls.c narrow_calls.h ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -c -o $@ narrow_calls.c

${PREFIX}_time64_test_arm: ${OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ ${OBJS} ${QEMU_SUPPORT} \
	    ${QEMU_ARM_LIBS}

check:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "time64 wrappers: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP time64 wrappers: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${PREFIX}_time64_test_arm
	${QEMU_ARM_RUN} ./${PREFIX}_time64_test_arm
	@sh ./check.sh "${MAKE}" "${QEMU_ARM_RUN}" ${GCCPREFIX}-nm
.endif

clean:
	rm -f good_*.o good_time64_test_arm mutant_*.o mutant_time64_test_arm \
	    mutant.out mutant-src/time64*
	if [ -d mutant-src ]; then rmdir mutant-src; fi

.PHONY: all check clean
