#
# Host tests of the PicoCalc console: the terminal emulator and key
# translation run against a model panel, under strict ISO C17 and the
# address and undefined-behavior sanitizers; the font table must match
# its BDF source byte for byte.
#
# Once the shipped sources pass, five mutants of them must fail: a
# terminal whose full-screen scroll forgets the hardware scroll start, one
# that skips the panel redraw after a region scroll, one that erases with
# the stored rather than the displayed background under reverse video, one
# that maps SGR 90-97 onto the base colors, a keymap that ignores Ctrl,
# a terminal that drops BEL, and one that rings on every other control
# byte. A harness that passed them would be checking nothing.
#
# kbdreq_test runs the keyboard-controller register mailbox against a
# model of the vendor firmware's register handler; eight mutants of
# picokbd_req.c and picokbd_req.h must fail it.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
CSTD=		-std=c17 -pedantic-errors -Wall -Wextra -Werror
SAN=		-fsanitize=address,undefined -fno-sanitize-recover=all
SRCS=		lcdterm_test.c ${DEV}/lcdterm.c ${DEV}/picokbd_map.c

all: check

check: lcdterm_test kbdreq_test font-sync
	./lcdterm_test
	./kbdreq_test
	${MAKE} mutants kbdreq-mutants

lcdterm_test: ${SRCS} ${DEV}/lcdterm.h ${DEV}/picokbd_map.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ ${SRCS}

kbdreq_test: kbdreq_test.c ${DEV}/picokbd_req.c ${DEV}/picokbd_req.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ kbdreq_test.c \
	    ${DEV}/picokbd_req.c

font-sync:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for font-sync" >&2; \
		exit 2; \
	fi
	"${PYTHON}" ${TOPSRC}/tools/picocalc/bdf_font.py --check \
	    ${DEV}/fonts/6x9-ascii.bdf ${DEV}/lcdfont.c

# Each mutant is the shipped source with one line changed by sed; the
# sed must change the file, and the test built from it must fail.
mutants: mutant-scroll mutant-region mutant-ctrl mutant-reverse-erase \
	mutant-bright mutant-bell mutant-bell-any

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

mutant-bright:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/sgr_fg = v - 90 + 8;/sgr_fg = v - 90;/'

mutant-bell:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/lcd_bell();/(void)0;/'

mutant-bell-any:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/break;\t\t\/\* SO, SI and the rest. \*\//lcd_bell(); break;/'

KBDREQ_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${CSTD}" "${DEV}" \
		kbdreq_test.c

KBDREQ_SRC=	picokbd_req.c picokbd_req.h

kbdreq-mutants: kbdreq-mutant-write-bit kbdreq-mutant-echo \
	kbdreq-mutant-retry kbdreq-mutant-expire-sent \
	kbdreq-mutant-percent kbdreq-mutant-level kbdreq-mutant-off-floor \
	kbdreq-mutant-off-ceiling

kbdreq-mutant-write-bit:
	${KBDREQ_MUT} picokbd_req.c \
	    's/q->reg | KBD_WRITE_BIT/q->reg/' ${KBDREQ_SRC}

kbdreq-mutant-echo:
	${KBDREQ_MUT} picokbd_req.c 's/if (b0 != q->reg)/if (0)/' \
	    ${KBDREQ_SRC}

kbdreq-mutant-retry:
	${KBDREQ_MUT} picokbd_req.c \
	    's/>= KBDREQ_TRIES/>= 100/' ${KBDREQ_SRC}

kbdreq-mutant-expire-sent:
	${KBDREQ_MUT} picokbd_req.c \
	    's/ \&\& q->state != KR_SENT)/)/' ${KBDREQ_SRC}

kbdreq-mutant-percent:
	${KBDREQ_MUT} picokbd_req.c \
	    's/return v \& ~KBD_BAT_CHARGING;/return v;/' ${KBDREQ_SRC}

kbdreq-mutant-level:
	${KBDREQ_MUT} picokbd_req.c \
	    's/return v < 0 ? 0 : v > KBD_LEVEL_MAX/return v > KBD_LEVEL_MAX/' \
	    ${KBDREQ_SRC}

kbdreq-mutant-off-floor:
	${KBDREQ_MUT} picokbd_req.h \
	    's/KBD_OFF_MIN		6/KBD_OFF_MIN		1/' ${KBDREQ_SRC}

kbdreq-mutant-off-ceiling:
	${KBDREQ_MUT} picokbd_req.c \
	    's/v > 255 ? 255 : v/v/' ${KBDREQ_SRC}

clean:
	rm -f lcdterm_test kbdreq_test

.PHONY: all check font-sync mutants mutant-scroll mutant-region \
	mutant-ctrl mutant-reverse-erase mutant-bright mutant-bell \
	mutant-bell-any clean \
	kbdreq-mutants kbdreq-mutant-write-bit kbdreq-mutant-echo \
	kbdreq-mutant-retry kbdreq-mutant-expire-sent \
	kbdreq-mutant-percent kbdreq-mutant-level kbdreq-mutant-off-floor \
	kbdreq-mutant-off-ceiling
