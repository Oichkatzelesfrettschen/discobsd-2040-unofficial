#
# Host tests of the PicoCalc console: the terminal emulator and key
# translation run against a model panel, under strict ISO C17 and the
# address and undefined-behavior sanitizers; the font table must match
# its BDF source byte for byte.
#
# Once the shipped sources pass, four mutants of them must fail: a
# terminal whose full-screen scroll forgets the hardware scroll start, one
# that skips the panel redraw after a region scroll, one that erases with
# the stored rather than the displayed background under reverse video, and
# a keymap that ignores Ctrl. A harness that passed them would be checking nothing.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
CSTD=		-std=c17 -pedantic-errors -Wall -Wextra -Werror
SAN=		-fsanitize=address,undefined -fno-sanitize-recover=all
SRCS=		lcdterm_test.c ${DEV}/lcdterm.c ${DEV}/picokbd_map.c

all: check

check: lcdterm_test font-sync
	./lcdterm_test
	${MAKE} mutants

lcdterm_test: ${SRCS} ${DEV}/lcdterm.h ${DEV}/picokbd_map.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ ${SRCS}

font-sync:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for font-sync" >&2; \
		exit 2; \
	fi
	"${PYTHON}" ${TOPSRC}/tools/picocalc/bdf_font.py --check \
	    ${DEV}/fonts/6x9-ascii.bdf ${DEV}/lcdfont.c

# Each mutant is the shipped source with one line changed by sed; the
# sed must change the file, and the test built from it must fail.
mutants: mutant-scroll mutant-region mutant-ctrl mutant-reverse-erase

mutant-scroll:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/lcd_set_top(top);/(void)top;/'

mutant-region:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/redraw_span(r, 0, LT_COLS - 1);/(void)r;/'

mutant-ctrl:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" picokbd_map.c \
	    's/\*ctrl = 1;/(void)0;/'

mutant-reverse-erase:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/sgr_rev ? sgr_fg : sgr_bg/sgr_bg/'

clean:
	rm -f lcdterm_test

.PHONY: all check font-sync mutants mutant-scroll mutant-region \
	mutant-ctrl mutant-reverse-erase clean
