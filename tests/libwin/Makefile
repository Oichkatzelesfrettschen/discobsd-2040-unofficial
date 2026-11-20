# Host gate for lib/libwin. See win_test.c.
#
#   check            native build, then the sanitized build where available,
#                    then the mutants, each of which must be rejected
#   check-native     the library under -Wall -Wextra -Werror -Wshadow
#                    -Wconversion against the model panel and the reference
#   check-sanitized  the same under AddressSanitizer and UBSan
#   check-mutants    one faulted copy of win.c per fault; each must exit 1
#                    with a FAIL line, and the unfaulted baseline must pass
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

.if empty(PYTHON)
.error PYTHON must name the intended interpreter
.endif

HOSTCC?=	cc
LIBDIR=		${TOPSRC}/lib/libwin
DEVDIR=		${TOPSRC}/sys/arch/rp2040/dev
# <sys/lcdgfx.h> is found after the host's own headers, so the tree's
# sys/types.h and sys/ioctl.h never shadow the host's.
BASE_FLAGS=	-std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra \
		-Werror -Wold-style-definition -Wshadow -Wconversion -O1 \
		-I${LIBDIR} -I${DEVDIR} -idirafter ${TOPSRC}/sys
LIB_SRCS=	${LIBDIR}/win.c ${DEVDIR}/lcdfont.c

# tools/host-test.mk supplies SAN_FLAGS and SAN_OK.
include ${TOPSRC}/tools/host-test.mk

all: check

win_test: win_test.c ${LIB_SRCS} ${LIBDIR}/win.h ${LIBDIR}/win_lcdgfx.c
	${HOSTCC} ${BASE_FLAGS} -o $@ win_test.c ${LIB_SRCS}

win_test_san: win_test.c ${LIB_SRCS} ${LIBDIR}/win.h ${LIBDIR}/win_lcdgfx.c
	${HOSTCC} ${BASE_FLAGS} ${SAN_FLAGS} -o $@ win_test.c ${LIB_SRCS}

check: check-native check-sanitized check-mutants

check-native: win_test
	./win_test

check-sanitized:
.if ${SAN_OK} == "yes"
	@${MAKE} win_test_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ./win_test_san
.else
	@echo "SKIP libwin sanitized: sanitizer runtime unavailable; no sanitized execution occurred"
.endif

mutants.stamp: win_mutate.py ${LIBDIR}/win.c
	rm -f win_*.mutant.c win_*.mutant.test win_*.mutant.log
	${PYTHON} -I win_mutate.py ${LIBDIR}/win.c .
	touch $@

check-mutants: mutants.stamp win_test.c check_mutants.sh
	HOSTCC=${HOSTCC} BASE_FLAGS="${BASE_FLAGS}" sh ./check_mutants.sh ${LIBDIR} ${DEVDIR}

clean:
	rm -f win_test win_test_san win_*.mutant.c win_*.mutant.test \
	    win_*.mutant.log mutants.stamp

.PHONY: all check check-native check-sanitized check-mutants clean
