# The members of the 64-bit view of time that are a second build of a
# narrow source -- ctime.c, mktime.c, difftime.c, time.c and wait3.c in
# lib/libc/gen, utime.c and ftime.c in lib/libc/compat, each compiled with
# _TIME_BITS defined as 64 -- executed as Thumb-1 under qemu-arm.
#
# calendar64_test.c checks gmtime, timegm, localtime, mktime, ctime and
# difftime against the fields vectors.py derives with CPython's datetime;
# calls64_test.c checks time, ftime, utime and wait3 through the wrappers
# they call (lib/libc/gen/time64_*.c) and the stand-in 32-bit entry points
# of tests/time64_wrappers. tzset.c, timezone.c and asctime.c are the
# narrow members both widths share; tzset.c reads its environment and the
# kernel's offset through zone_hooks.c. No kernel runs.
#
# CAL_SRC names the directory holding ctime.c, mktime.c, difftime.c,
# tzset.c and their headers, and PREFIX the names of the products;
# check.sh sets both to build each mutated copy, which the test must
# reject. check.sh also reads every member with nm. REQUIRE_QEMU makes a
# missing qemu-arm an error; otherwise the target prints SKIP and no ARM
# execution has occurred.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

GEN=		${TOPSRC}/lib/libc/gen
COMPAT=		${TOPSRC}/lib/libc/compat
WRAPPERS=	${TOPSRC}/tests/time64_wrappers
CAL_SRC?=	${GEN}
PREFIX?=	good

TREE_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -Os ${QEMU_ARM_CPUFLAGS} \
		-ffreestanding -fno-common -nostdinc -I${TOPSRC}/include
WIDE=		-D_TIME_BITS=64
# The 32-bit members, built as lib/libc/Makefile builds them.
NARROW=		-D_TIME_BITS=32
ZONE_RENAMES=	-Dgetenv=calendar_getenv -Dgettimeofday=calendar_zone_gettimeofday
# The headers that carry the two views; an object is rebuilt when one moves.
VIEW_HEADERS=	${TOPSRC}/sys/sys/_time_t.h ${TOPSRC}/include/time.h \
		${TOPSRC}/include/utime.h ${TOPSRC}/sys/sys/timeb.h \
		${TOPSRC}/sys/sys/time.h ${TOPSRC}/sys/sys/wait.h \
		${TOPSRC}/sys/sys/resource.h
CAL_HEADERS=	${CAL_SRC}/offtime.h ${CAL_SRC}/tzstate.h ${VIEW_HEADERS}

# Members whose source a mutant may change, those it may not, the wrappers
# the calls reach, and the narrow members both widths link.
CAL_MEMBERS=	ctime mktime difftime
GEN_MEMBERS=	time wait3
COMPAT_MEMBERS=	utime ftime
WRAPPER_MEMBERS= time64_gettimeofday time64_utimes time64_wait4 \
		time64_widen_rusage

OBJS=		${CAL_MEMBERS:S,^,${PREFIX}_,:S,$,64.o,} \
		${GEN_MEMBERS:S,^,${PREFIX}_,:S,$,64.o,} \
		${COMPAT_MEMBERS:S,^,${PREFIX}_,:S,$,64.o,} \
		${WRAPPER_MEMBERS:S,^,${PREFIX}_,:S,$,.o,} \
		${PREFIX}_tzset.o ${PREFIX}_timezone.o ${PREFIX}_asctime.o \
		${PREFIX}_zone_hooks.o ${PREFIX}_narrow.o ${PREFIX}_test.o \
		${PREFIX}_calls.o
# The same members at the 32-bit width, read by check.sh alone.
NARROW_OBJS=	${CAL_MEMBERS:S,^,${PREFIX}_,:S,$,32.o,} \
		${GEN_MEMBERS:S,^,${PREFIX}_,:S,$,32.o,} \
		${COMPAT_MEMBERS:S,^,${PREFIX}_,:S,$,32.o,}

all: check

calendar64_vectors.h: vectors.py
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	"$${PYTHON}" vectors.py $@

.for member in ${CAL_MEMBERS}
${PREFIX}_${member}64.o: ${CAL_SRC}/${member}.c ${CAL_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${WIDE} -c -o $@ ${CAL_SRC}/${member}.c
${PREFIX}_${member}32.o: ${CAL_SRC}/${member}.c ${CAL_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ ${CAL_SRC}/${member}.c
.endfor

.for member in ${GEN_MEMBERS}
${PREFIX}_${member}64.o: ${GEN}/${member}.c ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${WIDE} -c -o $@ ${GEN}/${member}.c
${PREFIX}_${member}32.o: ${GEN}/${member}.c ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ ${GEN}/${member}.c
.endfor

.for member in ${COMPAT_MEMBERS}
${PREFIX}_${member}64.o: ${COMPAT}/${member}.c ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${WIDE} -c -o $@ ${COMPAT}/${member}.c
${PREFIX}_${member}32.o: ${COMPAT}/${member}.c ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ ${COMPAT}/${member}.c
.endfor

.for member in ${WRAPPER_MEMBERS}
${PREFIX}_${member}.o: ${GEN}/${member}.c ${GEN}/time64.h ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -c -o $@ ${GEN}/${member}.c
.endfor

${PREFIX}_tzset.o: ${CAL_SRC}/tzset.c ${CAL_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} ${ZONE_RENAMES} -c -o $@ \
	    ${CAL_SRC}/tzset.c

${PREFIX}_timezone.o: ${GEN}/timezone.c
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -Wno-old-style-definition \
	    -Wno-deprecated-non-prototype -c -o $@ ${GEN}/timezone.c

${PREFIX}_asctime.o: ${GEN}/asctime.c
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ ${GEN}/asctime.c

${PREFIX}_zone_hooks.o: zone_hooks.c zone_hooks.h ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ zone_hooks.c

${PREFIX}_narrow.o: ${WRAPPERS}/narrow_calls.c ${WRAPPERS}/narrow_calls.h \
    ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} ${NARROW} -c -o $@ \
	    ${WRAPPERS}/narrow_calls.c

${PREFIX}_test.o: calendar64_test.c calendar64_vectors.h zone_hooks.h \
    ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -DCALENDAR64_CALLS -I. -c -o $@ \
	    calendar64_test.c

${PREFIX}_calls.o: calls64_test.c ${WRAPPERS}/narrow_calls.h ${VIEW_HEADERS}
	${QEMU_ARM_CC} ${TREE_CFLAGS} -I${WRAPPERS} -c -o $@ calls64_test.c

${PREFIX}_calendar64_test_arm: ${OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ ${OBJS} ${QEMU_SUPPORT} \
	    ${QEMU_ARM_LIBS}

check:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "time64 calendar: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP time64 calendar: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${PREFIX}_calendar64_test_arm ${NARROW_OBJS}
	${QEMU_ARM_RUN} ./${PREFIX}_calendar64_test_arm
	@sh ./check.sh "${MAKE}" "${QEMU_ARM_RUN}" ${GCCPREFIX}-nm \
	    "${QEMU_ARM_CC} ${TREE_CFLAGS}"
.endif

clean:
	rm -f good_*.o good_calendar64_test_arm mutant_*.o \
	    mutant_calendar64_test_arm mutant.out calendar64_vectors.h \
	    calibration_*.o mutant-src/*.c mutant-src/*.h
	if [ -d mutant-src ]; then rmdir mutant-src; fi

.PHONY: all check clean
