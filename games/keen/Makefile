TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/games
PROG=		keen
OBJS=		${PROG}.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
SOLVER=		./tests/keensolve
CHECKED_SIZES=	3 4 5 6
SEED_RANGES=	1-10 11-20 21-30 31-40
UNIQUENESS_TARGETS=	test-uniqueness-fixtures \
			test-uniqueness-unchecked-4 \
			test-uniqueness-unchecked-5

.for size in ${CHECKED_SIZES}
.for seed_range in ${SEED_RANGES}
UNIQUENESS_TARGETS+=	test-uniqueness-checked-${size}-${seed_range}
.endfor
.endfor

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Same source, host compiler; see games/fifteen/Makefile.
host:		${PROG}.c ${TOPSRC}/games/gametty.h
		${HOSTCC} -std=c99 -Wall -Wextra -Werror -DHOSTBUILD -o ${HOSTBIN} ${PROG}.c

${SOLVER}:	tests/keensolve.c
		${HOSTCC} -std=c17 -O2 -Wall -Wextra -Werror -o $@ $>

test-play:	host
		${PYTHON} tests/play.py ./${HOSTBIN}

test-uniqueness-fixtures:	host ${SOLVER}
		${PYTHON} tests/uniqueness.py fixtures ./${HOSTBIN} ${SOLVER}

test-uniqueness-denominator:
		@set -- ${UNIQUENESS_TARGETS}; \
		[ "$$#" -eq 19 ] || { \
			echo "keen: expected 19 uniqueness shards, found $$#" >&2; \
			exit 1; \
		}
		@echo "keen: denominator 4 fixtures + 80 unchecked + 160 checked = 244 paired counts"

.for size in 4 5
test-uniqueness-unchecked-${size}:	host ${SOLVER}
		${PYTHON} tests/uniqueness.py unchecked-${size} ./${HOSTBIN} ${SOLVER}
.endfor

.for size in ${CHECKED_SIZES}
.for seed_range in ${SEED_RANGES}
test-uniqueness-checked-${size}-${seed_range}:	host ${SOLVER}
		${PYTHON} tests/uniqueness.py checked-${size}-${seed_range} \
		    ./${HOSTBIN} ${SOLVER}
.endfor
.endfor

test:		test-play test-uniqueness-denominator ${UNIQUENESS_TARGETS}

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN} ${SOLVER}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		test test-play test-uniqueness-denominator ${UNIQUENESS_TARGETS}
