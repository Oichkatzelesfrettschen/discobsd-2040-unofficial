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
# byte. Eight more mutants break the glyph bank: an upload that does not
# store, one that does not redraw the cells showing the code, a terminal that
# prints bytes above 0x7e as '?', one that lets C1 bytes reach the bank, a
# line-drawing map shifted by one code, a full reset that keeps uploads, and
# uploads that skip the row check or the code check. A harness that passed
# them would be checking nothing.
#
# Ten more mutants break the terminal's pixel mode: a terminal that draws
# cells, fills or scroll starts while a pixel program owns the panel, one
# whose repaint skips the last row, the scroll start or the cursor, one
# whose mode switch skips the repaint or the text configuration, one that
# repaints on entering the mode, and a fatal path that does not leave the
# mode. lcdgfx_test drives the /dev/lcdgfx
# driver (lcdgfx.c) and the real terminal against a model of the panel's
# write path; sixty-one mutants of lcdgfx.c and lcdterm.c must fail it:
# a skipped exclusive open, restore, range check, window abandonment or
# scroll-start update, wrong color widening, byte order, palette
# handling, bit-row padding, line, circle and frame drawing, and, for the
# tile atlas, a moved upload or block bound, a wrong upload offset or clamp,
# a tile number accepted past the atlas, tile pixel lines drawn tile by tile
# instead of interleaved, wrong nibble order, palette index or line stride,
# a block that does not advance its row, an upload or block that survives
# another command, an atlas that survives an open and a wrong atlas size.
#
# kbdreq_test runs the keyboard-controller register mailbox against a
# model of the vendor firmware's register handler; eight mutants of
# picokbd_req.c and picokbd_req.h must fail it.
#
# kbdevq_test runs the raw key event queue (picokbd_evq.c) beside the key
# translation: every state the controller reports must enter the queue in
# order, a full queue drops its oldest event and counts the drop, nothing is
# recorded outside a session, a session needs a readable descriptor in the
# foreground group and in RAW or CBREAK mode, belongs to its process, ends
# on close, on the process's exit, on request, on a change of foreground
# group and on any change of the tty's mode word (a getpass-like ECHO
# clear), and leaves the queue empty, and the translated bytes must equal
# those of picokbd_translate() alone; forty-four mutants of picokbd_evq.c
# and sixteen cut connections in the kernel glue (wiring.sh) must fail it.
#
# gametile_test runs the tile library (games/gametile.h) against a model of
# the tile commands of /dev/lcdgfx; twenty-nine mutants of the header must
# fail it.
#
# sdspi_test runs the SD card protocol layer against a card model that
# replays recorded command transcripts, including the crash dump's window
# reads and summed span writes; twenty-three mutants of sdspi.c must fail
# it.
#
# sdstrat_test compiles the SD disk driver (sd.c) unchanged against the
# kernel's buf.h and disk.h through the headers in shim/ and a card behind
# fake sdspi calls, then drives sdopen, sdstrategy and sdsize with the
# requests physio() and the buffer cache make, calls the probe under each
# board identity, and writes crash dumps through sddump() and the kernel's
# dump stream (rp2040/dump.c) onto the card's dump partition, mounted,
# beside a mount, undersized, failing and replaced; fifty-six mutants of
# sd.c and dump.c must fail it.
#
# dump_test writes a dump with the kernel's dump stream into a partition
# image and runs savecore(8) on it from the host: a saved dump, a damaged
# one, too little space and a blocked copy; nine mutants of savecore.c
# must fail it.
#
# picoaudio_test compiles the bell driver (picoaudio.c) unchanged against
# the register model in regmodel.h, stops the tone at every counter phase
# and reads each amplifier pin's level from the registers the driver left
# under the PWM slice's double-buffered compare and the GPIO output
# override; six mutants of picoaudio.c must fail it.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
# The CRC-32 the crash dump and the packed executables share; the kernel
# header it includes comes from the tree after the host's own.
CRC32=		${TOPSRC}/sys/kern/subr_crc32.c
# The crash-dump stream sd.c's sddump() writes through, and savecore(8),
# which dump_test runs on the host with main renamed. _DEFAULT_SOURCE makes
# glibc declare getopt, fsync and mkdtemp under -std=c17, as kbdevq_test
# does; macOS declares them unless a strict POSIX level is requested, and
# _POSIX_C_SOURCE=200809L there hides mkdtemp.
DUMPW=		${TOPSRC}/sys/arch/rp2040/rp2040/dump.c
SAVECORE=	${TOPSRC}/sbin/savecore
DUMPCSTD=	-std=c17 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
		-Dmain=savecore_main -I${TOPSRC}/tests/picocalc/shim \
		-I${TOPSRC}/sys/arch -I${SAVECORE} -idirafter ${TOPSRC}/sys
CSTD=		-std=c17 -pedantic-errors -Wall -Wextra -Werror
SAN=		-fsanitize=address,undefined -fno-sanitize-recover=all
# The kernel compiles under the GNU17 profile, and sd.c uses the BSD types
# that profile exposes.
KCSTD=		-std=gnu17 -Wall -Wextra -Werror \
		-DBUS_KHZ=125000 -DCPU_KHZ=125000 \
		-I${TOPSRC}/tests/picocalc/shim -I${TOPSRC}/sys/arch
SRCS=		lcdterm_test.c ${DEV}/lcdterm.c ${DEV}/lcdfont.c ${DEV}/picokbd_map.c

all: check

check: lcdterm_test lcdgfx_test kbdreq_test kbdevq_test gametile_test \
	sdspi_test sdstrat_test dump_test picoaudio_test font-sync
	./lcdterm_test
	./lcdgfx_test
	./kbdreq_test
	./kbdevq_test
	./gametile_test
	./sdspi_test
	./sdstrat_test
	./dump_test
	./picoaudio_test
	${MAKE} mutants gfx-mutants kbdreq-mutants kbdevq-mutants \
	    gametile-mutants sdspi-mutants sdstrat-mutants dump-mutants \
	    picoaudio-mutants

lcdterm_test: ${SRCS} ${DEV}/lcdterm.h ${DEV}/lcdfont.h ${DEV}/picokbd_map.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ ${SRCS}

lcdgfx_test: lcdgfx_test.c ${DEV}/lcdgfx.c ${DEV}/lcdgfx.h ${DEV}/lcdterm.c \
	    ${DEV}/lcdterm.h ${DEV}/lcdfont.c ${DEV}/lcdfont.h \
	    ${TOPSRC}/sys/sys/lcdgfx.h
	${HOST_CC} ${KCSTD} ${SAN} -g -o $@ lcdgfx_test.c ${DEV}/lcdgfx.c \
	    ${DEV}/lcdterm.c ${DEV}/lcdfont.c

kbdreq_test: kbdreq_test.c ${DEV}/picokbd_req.c ${DEV}/picokbd_req.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ kbdreq_test.c \
	    ${DEV}/picokbd_req.c

# The raw key event queue compiles as strict ISO C17 against the public
# console header; _DEFAULT_SOURCE supplies the BSD u_char the header uses.
kbdevq_test: kbdevq_test.c ${DEV}/picokbd_evq.c ${DEV}/picokbd_evq.h \
	    ${DEV}/picokbd_map.c ${DEV}/picokbd_map.h ${TOPSRC}/sys/sys/lcdcons.h
	${HOST_CC} ${CSTD} -D_DEFAULT_SOURCE -DPICOKBD_EVQ_TEST ${SAN} -g \
	    -I${TOPSRC}/tests/picocalc/shim -I${DEV} -o $@ kbdevq_test.c ${DEV}/picokbd_evq.c ${DEV}/picokbd_map.c

# The tile library is a header of static functions, strict C17 like the games
# that include it; the test binds its ioctl and write hooks to a model.
gametile_test: gametile_test.c ${TOPSRC}/games/gametile.h \
	    ${TOPSRC}/sys/sys/lcdgfx.h
	${HOST_CC} ${CSTD} -D_DEFAULT_SOURCE ${SAN} -g -I${TOPSRC}/tests/picocalc/shim \
	    -I${TOPSRC}/games -o $@ gametile_test.c

sdspi_test: sdspi_test.c ${DEV}/sdspi.c ${DEV}/sdspi.h ${CRC32}
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -idirafter ${TOPSRC}/sys \
	    -o $@ sdspi_test.c ${DEV}/sdspi.c ${CRC32}

sdstrat_test: sdstrat_test.c regmodel.h ${DEV}/sd.c ${DEV}/sd.h \
	    ${DEV}/sdspi.h ${DEV}/picocalc_reg.h shim/machine/board.h \
	    shim/machine/dump.h ${TOPSRC}/sys/arch/rp2040/include/board.h \
	    ${TOPSRC}/sys/arch/rp2040/include/dump.h ${DUMPW} ${CRC32}
	${HOST_CC} ${KCSTD} ${SAN} -g -idirafter ${TOPSRC}/sys -o $@ \
	    sdstrat_test.c ${DEV}/sd.c ${DUMPW} ${CRC32}

dump_test: dump_test.c ${DUMPW} ${CRC32} ${SAVECORE}/savecore.c \
	    ${SAVECORE}/savecore.h shim/machine/dump.h \
	    ${TOPSRC}/sys/arch/rp2040/include/dump.h
	${HOST_CC} ${DUMPCSTD} ${SAN} -g -o $@ dump_test.c ${DUMPW} \
	    ${CRC32} ${SAVECORE}/savecore.c

# Each mutant of savecore.c must fail dump_test. The header's length is
# checked twice, against the table before the space test and against the
# segments at the end of the copy, so no single cut of either fails it.
DUMP_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${DUMPCSTD}" "${SAVECORE}" \
		dump_test.c
DUMP_SRC=	savecore.c savecore.h ${DUMPW} ${CRC32}

dump-mutants: \
	dump-mutant-early-clear \
	dump-mutant-segment-sum-unchecked \
	dump-mutant-header-sum-unchecked \
	dump-mutant-version-unchecked \
	dump-mutant-magic-unchecked \
	dump-mutant-reserve-dropped \
	dump-mutant-minfree-ignored \
	dump-mutant-bounds-stuck \
	dump-mutant-temp-kept

dump-mutant-early-clear:
	${DUMP_MUT} savecore.c \
	    's/bounds = read_number("bounds", 0);/clear_dump(dev); bounds = read_number("bounds", 0);/' ${DUMP_SRC}

dump-mutant-segment-sum-unchecked:
	${DUMP_MUT} savecore.c \
	    's/if (~crc != s.ds_crc)/if (0)/' ${DUMP_SRC}

dump-mutant-header-sum-unchecked:
	${DUMP_MUT} savecore.c \
	    's/if (crc != h.dh_crc)/if (crc == ~h.dh_crc)/' ${DUMP_SRC}

dump-mutant-version-unchecked:
	${DUMP_MUT} savecore.c \
	    's/if (h.dh_version != DUMP_VERSION)/if (0)/' ${DUMP_SRC}

dump-mutant-magic-unchecked:
	${DUMP_MUT} savecore.c \
	    's/if (h.dh_magic != DUMP_MAGIC) {/if (0) {/' ${DUMP_SRC}

dump-mutant-reserve-dropped:
	${DUMP_MUT} savecore.c \
	    's/h.dh_bytes + INFO_BYTES + reserve) {/h.dh_bytes + INFO_BYTES) {/' ${DUMP_SRC}

dump-mutant-minfree-ignored:
	${DUMP_MUT} savecore.c \
	    's/reserve = read_number("minfree", -1);/reserve = -1;/' ${DUMP_SRC}

dump-mutant-bounds-stuck:
	${DUMP_MUT} savecore.c \
	    's/bounds + 1);/bounds);/' ${DUMP_SRC}

dump-mutant-temp-kept:
	${DUMP_MUT} savecore.c \
	    's/unlink(tmp);/(void)0;/' ${DUMP_SRC}

picoaudio_test: picoaudio_test.c regmodel.h ${DEV}/picoaudio.c \
	    ${DEV}/picocalc_reg.h
	${HOST_CC} ${KCSTD} ${SAN} -g -o $@ picoaudio_test.c ${DEV}/picoaudio.c

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
	mutant-bright mutant-bell mutant-bell-any mutant-bank-store \
	mutant-bank-redraw mutant-bank-ascii mutant-bank-c1 \
	mutant-bank-acs-shift mutant-bank-ris mutant-bank-rows \
	mutant-bank-code mutant-gfx-gate-cell mutant-gfx-gate-fill \
	mutant-gfx-gate-top mutant-gfx-repaint-row mutant-gfx-repaint-top \
	mutant-gfx-repaint-cursor mutant-gfx-leave-repaint \
	mutant-gfx-leave-reset mutant-gfx-abort mutant-gfx-enter-draws

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

mutant-bank-store:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/^\t\tg\[i\] = rows\[i\];/\t\t(void)g;/'

mutant-bank-redraw:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_setglyph/,/^}/s/^\t\t\t\tdraw(r, c);/\t\t\t\t(void)r;/'

mutant-bank-ascii:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/if (c < LT_C1_END)/if (c <= 0xff)/'

mutant-bank-c1:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/if (c < LT_C1_END)/if (c < LT_GLYPH_FIRST)/'

mutant-bank-acs-shift:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/(c - LT_ACS_FIRST);/(c - LT_ACS_FIRST + 1);/'

mutant-bank-ris:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/^\t\tglyph_defaults();/\t\t(void)0;/'

mutant-bank-rows:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    's/if ((rows\[i\] \& ~LT_GLYPH_MASK) != 0)/if (0)/'

mutant-bank-code:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_setglyph/,/^}/s/code >= LT_GLYPH_FIRST + LT_GLYPH_N/code > 0x1ff/'

mutant-gfx-gate-cell:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^panel_cell/,/^}/s/if (!gfx_active)/if (1)/'

mutant-gfx-gate-fill:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^panel_fill/,/^}/s/if (!gfx_active)/if (1)/'

mutant-gfx-gate-top:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^panel_top/,/^}/s/if (!gfx_active)/if (1)/'

mutant-gfx-repaint-row:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_repaint/,/^}/s/p < LT_ROWS; p++/p < LT_ROWS - 1; p++/'

mutant-gfx-repaint-top:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_repaint/,/^}/s/lcd_set_top(top);/(void)top;/'

mutant-gfx-repaint-cursor:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_repaint/,/^}/s/lcdterm_cursor_show();/(void)0;/'

mutant-gfx-leave-repaint:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/lcdterm_repaint();/(void)0;/'

mutant-gfx-leave-reset:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/lcd_gfx_reset();/(void)0;/'

# Entering pixel mode must leave the panel to the pixel device: a repaint on
# entry draws over what the program is about to show.
mutant-gfx-enter-draws:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/^[[:space:]]*gfx_active = on;$$/gfx_active = on ? (lcdterm_repaint(), on) : 0;/'

mutant-gfx-abort:
	@sh ./mutant.sh "${HOST_CC}" "${CSTD}" "${DEV}" lcdterm.c \
	    '/^lcdterm_abort/,/^}/s/lcdterm_gfx(0);/lcd_set_top(top);/'

KBDEVQ_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" \
		"${CSTD} -D_DEFAULT_SOURCE -DPICOKBD_EVQ_TEST -I${TOPSRC}/tests/picocalc/shim" \
		"${DEV}" kbdevq_test.c

KBDEVQ_SRC=	picokbd_evq.c picokbd_evq.h picokbd_map.c picokbd_map.h

kbdevq-mutants: \
	kbdevq-mutant-drop-newest \
	kbdevq-mutant-lost-uncounted \
	kbdevq-mutant-lost-wraps \
	kbdevq-mutant-lost-kept \
	kbdevq-mutant-drain-limit \
	kbdevq-mutant-release-dropped \
	kbdevq-mutant-hold-dropped \
	kbdevq-mutant-idle-queued \
	kbdevq-mutant-key-masked \
	kbdevq-mutant-translate-dropped \
	kbdevq-mutant-drain-slot \
	kbdevq-mutant-put-slot \
	kbdevq-mutant-full-threshold \
	kbdevq-mutant-put-ungated \
	kbdevq-mutant-drain-unreadable \
	kbdevq-mutant-drain-background \
	kbdevq-mutant-drain-no-session \
	kbdevq-mutant-start-unreadable \
	kbdevq-mutant-start-background \
	kbdevq-mutant-start-keeps-queue \
	kbdevq-mutant-start-steals \
	kbdevq-mutant-stop-any-file \
	kbdevq-mutant-fileclose-ignored \
	kbdevq-mutant-fileclose-any-file \
	kbdevq-mutant-check-ignored \
	kbdevq-mutant-check-always-ends \
	kbdevq-mutant-end-keeps-queue \
	kbdevq-mutant-end-keeps-drops \
	kbdevq-mutant-end-keeps-bytes \
	kbdevq-mutant-check-ignores-group \
	kbdevq-mutant-check-ignores-mode \
	kbdevq-mutant-mode-counts-line-discipline \
	kbdevq-mutant-start-cooked \
	kbdevq-mutant-start-raw-only \
	kbdevq-mutant-start-no-snapshot \
	kbdevq-mutant-modeset-ignored \
	kbdevq-mutant-modeset-owner \
	kbdevq-mutant-drain-any-process \
	kbdevq-mutant-stop-any-process \
	kbdevq-mutant-start-any-process \
	kbdevq-mutant-procexit-ignored \
	kbdevq-mutant-procexit-any-process \
	kbdevq-mutant-owner-pid-ignored \
	kbdevq-mutant-owner-proc-ignored \
	kbdevq-mutant-wiring

kbdevq-mutant-drop-newest:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/^\t\ttail++;$$/\t\treturn;/' ${KBDEVQ_SRC}

kbdevq-mutant-lost-uncounted:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/^\t\t\tlost++;$$/\t\t\t(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-lost-wraps:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/if (lost < 255)/if (1)/' ${KBDEVQ_SRC}

kbdevq-mutant-lost-kept:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_drain/,/^}/s/^\tlost = 0;$$/\t(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-limit:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/n < LCD_KEYEV_N \&\& tail != head/n < LCD_KEYEV_N + 1 \&\& tail != head/' ${KBDEVQ_SRC}

kbdevq-mutant-release-dropped:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/^\t    state != PICOKBD_RELEASED)$$/\t    state != PICOKBD_PRESSED)/' ${KBDEVQ_SRC}

kbdevq-mutant-hold-dropped:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/state != PICOKBD_HOLD \&\&/state != PICOKBD_PRESSED \&\&/' ${KBDEVQ_SRC}

kbdevq-mutant-idle-queued:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/^\t    state != PICOKBD_RELEASED)$$/\t    state > 3)/' ${KBDEVQ_SRC}

kbdevq-mutant-key-masked:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/picokbd_evq_put(state, key);$$/picokbd_evq_put(state, key \& 0x7f);/' ${KBDEVQ_SRC}

kbdevq-mutant-translate-dropped:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/return picokbd_translate(state, key, appcursor, ctrl, out);/return (void)appcursor, (void)ctrl, (void)out, 0;/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-slot:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/queue\[tail++ \& (LCD_KEYEV_QUEUE - 1)\]/queue[tail++ \& (LCD_KEYEV_QUEUE - 2)]/' ${KBDEVQ_SRC}

kbdevq-mutant-put-slot:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/head++ \& (LCD_KEYEV_QUEUE - 1)/head++ % (LCD_KEYEV_QUEUE - 1)/' ${KBDEVQ_SRC}

kbdevq-mutant-full-threshold:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/head - tail >= LCD_KEYEV_QUEUE/head - tail > LCD_KEYEV_QUEUE/' ${KBDEVQ_SRC}

kbdevq-mutant-put-ungated:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_put/,/^}/s/if (!active)/if (!active \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-unreadable:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_drain/,/^}/s/if (!readable)/if (!readable \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-background:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_drain/,/^}/s/pgrp == 0 || pgrp != tty_pgrp/0 \&\& (pgrp == 0 || pgrp != tty_pgrp)/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-no-session:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_drain/,/^}/s/if (!active)/if (!active \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-start-unreadable:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/if (!readable)/if (!readable \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-start-background:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/pgrp == 0 || pgrp != tty_pgrp/0 \&\& (pgrp == 0 || pgrp != tty_pgrp)/' ${KBDEVQ_SRC}

kbdevq-mutant-start-keeps-queue:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/^\tsession_end();$$/\t(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-start-steals:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/owner != file || !is_owner(proc, pid)/(owner != file \&\& 0) || !is_owner(proc, pid)/' ${KBDEVQ_SRC}

kbdevq-mutant-stop-any-file:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_stop/,/^}/s/owner != file || !is_owner(proc, pid)/(owner != file \&\& 0) || !is_owner(proc, pid)/' ${KBDEVQ_SRC}

kbdevq-mutant-fileclose-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_fileclose/,/^}/s/session_end();/(void)file;/' ${KBDEVQ_SRC}

kbdevq-mutant-fileclose-any-file:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_fileclose/,/^}/s/if (active \&\& owner == file)/if (active \&\& (owner == file || 1))/' ${KBDEVQ_SRC}

kbdevq-mutant-check-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_check/,/^}/s/session_end();/(void)tty_pgrp, (void)tty_flags;/' ${KBDEVQ_SRC}

kbdevq-mutant-check-always-ends:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_check/,/^}/s/owner_pgrp != tty_pgrp ||/(owner_pgrp != tty_pgrp || 1) ||/' ${KBDEVQ_SRC}

kbdevq-mutant-end-keeps-queue:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^session_end/,/^}/s/^\thead = tail = 0;$$/\t(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-end-keeps-bytes:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^session_end/,/^}/s/queue\[i\].key = 0;/(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-end-keeps-drops:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^session_end/,/^}/s/^\tlost = 0;$$/\t(void)0;/' ${KBDEVQ_SRC}

kbdevq-mutant-drain-any-process:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_drain/,/^}/s/if (!is_owner(proc, pid))/if (!is_owner(proc, pid) \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-stop-any-process:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_stop/,/^}/s/ || !is_owner(proc, pid))/ || (!is_owner(proc, pid) \&\& 0))/' ${KBDEVQ_SRC}

kbdevq-mutant-start-any-process:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/ || !is_owner(proc, pid))/ || (!is_owner(proc, pid) \&\& 0))/' ${KBDEVQ_SRC}

kbdevq-mutant-procexit-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_procexit/,/^}/s/session_end();/(void)proc, (void)pid;/' ${KBDEVQ_SRC}

kbdevq-mutant-procexit-any-process:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_procexit/,/^}/s/if (active \&\& is_owner(proc, pid))/if (active \&\& (is_owner(proc, pid) || 1))/' ${KBDEVQ_SRC}

kbdevq-mutant-owner-pid-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/return owner_proc == proc \&\& owner_pid == pid;/return owner_proc == proc \&\& (owner_pid == pid || 1);/' ${KBDEVQ_SRC}

kbdevq-mutant-owner-proc-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    's/return owner_proc == proc \&\& owner_pid == pid;/return (owner_proc == proc || 1) \&\& owner_pid == pid;/' ${KBDEVQ_SRC}

kbdevq-mutant-check-ignores-group:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_check/,/^}/s/owner_pgrp != tty_pgrp ||/(owner_pgrp != tty_pgrp \&\& 0) ||/' ${KBDEVQ_SRC}

kbdevq-mutant-check-ignores-mode:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_check/,/^}/s/owner_mode != mode_of(tty_flags)/(owner_mode != mode_of(tty_flags) \&\& 0)/' ${KBDEVQ_SRC}

kbdevq-mutant-mode-counts-line-discipline:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^mode_of/,/^}/s/tty_flags \& ~(unsigned)PICOKBD_EVQ_VOLATILE/tty_flags/' ${KBDEVQ_SRC}

kbdevq-mutant-start-cooked:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/(tty_flags \& PICOKBD_EVQ_RAW_OR_CBREAK) == 0/(tty_flags \& PICOKBD_EVQ_RAW_OR_CBREAK) == 0 \&\& 0/' ${KBDEVQ_SRC}

kbdevq-mutant-start-raw-only:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/(tty_flags \& PICOKBD_EVQ_RAW_OR_CBREAK) == 0/(tty_flags \& 0x20U) == 0/' ${KBDEVQ_SRC}

kbdevq-mutant-start-no-snapshot:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_start/,/^}/s/owner_mode = mode_of(tty_flags);/owner_mode = 0;/' ${KBDEVQ_SRC}

kbdevq-mutant-modeset-ignored:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_modeset/,/^}/s/session_end();/(void)proc, (void)pid;/' ${KBDEVQ_SRC}

kbdevq-mutant-modeset-owner:
	${KBDEVQ_MUT} picokbd_evq.c \
	    '/^picokbd_evq_modeset/,/^}/s/if (active \&\& !is_owner(proc, pid))/if (active \&\& (!is_owner(proc, pid) || 1))/' ${KBDEVQ_SRC}

# The kernel glue that no host program runs (the poller, the console's
# request handlers, the close hook in vn_closefile) is checked by reading
# the function that must make each call; wiring.sh states the checks and
# cuts each connection in turn to show that it rejects the cut.
kbdevq-mutant-wiring:
	@sh ./wiring.sh "${DEV}/picokbd.c" "${DEV}/lcdcons.c" \
	    "${TOPSRC}/sys/kern/vfs_vnops.c" "${TOPSRC}/sys/kern/kern_exit.c" \
	    "${TOPSRC}/sys/kern/exec_subr.c"
	@sh ./wiring.sh -m "${DEV}/picokbd.c" "${DEV}/lcdcons.c" \
	    "${TOPSRC}/sys/kern/vfs_vnops.c" "${TOPSRC}/sys/kern/kern_exit.c" \
	    "${TOPSRC}/sys/kern/exec_subr.c"

# The sanitizers are on for the mutants: a range check that forms an
# overflowing sum is rejected by the test only because UBSan stops it.
GAMETILE_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" \
		"${CSTD} ${SAN} -D_DEFAULT_SOURCE -I${TOPSRC}/tests/picocalc/shim" \
		"${TOPSRC}/games" gametile_test.c

GAMETILE_SRC=	gametile.h

gametile-mutants: \
	gametile-mutant-run-crosses-row \
	gametile-mutant-run-single \
	gametile-mutant-run-bridges-gap \
	gametile-mutant-mark-kept \
	gametile-mutant-pending-uncounted \
	gametile-mutant-pending-kept \
	gametile-mutant-set-always-marks \
	gametile-mutant-set-unmarked \
	gametile-mutant-set-tile-range \
	gametile-mutant-set-x-bound \
	gametile-mutant-init-clean \
	gametile-mutant-fill-range \
	gametile-mutant-fill-tile-range \
	gametile-mutant-short-write-accepted \
	gametile-mutant-ioctl-failure-ignored \
	gametile-mutant-window-height \
	gametile-mutant-write-source \
	gametile-mutant-load-last-tile \
	gametile-mutant-load-high \
	gametile-mutant-load-count-zero \
	gametile-mutant-load-touch-missing \
	gametile-mutant-touch-range \
	gametile-mutant-load-bytes \
	gametile-mutant-fill-column-sum \
	gametile-mutant-fill-row-sum \
	gametile-mutant-fill-origin-unchecked \
	gametile-mutant-load-sum \
	gametile-mutant-load-product \
	gametile-mutant-touch-unchecked

gametile-mutant-run-crosses-row:
	${GAMETILE_MUT} gametile.h \
	    's/x + run < GTILE_COLS \&\&$$/1 \&\&/' ${GAMETILE_SRC}

gametile-mutant-run-single:
	${GAMETILE_MUT} gametile.h \
	    's/while (x + run < GTILE_COLS \&\&$$/while (0 \&\&/' ${GAMETILE_SRC}

gametile-mutant-run-bridges-gap:
	${GAMETILE_MUT} gametile.h \
	    's/^\t\t\t    gtile_is_dirty(g, y \* GTILE_COLS + x + run))$$/\t\t\t    (gtile_is_dirty(g, y * GTILE_COLS + x + run) || gtile_is_dirty(g, y * GTILE_COLS + x + run + 1)))/' ${GAMETILE_SRC}

gametile-mutant-mark-kept:
	${GAMETILE_MUT} gametile.h \
	    's/gtile_unmark(g, y \* GTILE_COLS + x + k);/(void)k;/' ${GAMETILE_SRC}

gametile-mutant-pending-uncounted:
	${GAMETILE_MUT} gametile.h \
	    's/g->pending++;/(void)0;/' ${GAMETILE_SRC}

gametile-mutant-pending-kept:
	${GAMETILE_MUT} gametile.h \
	    's/g->pending--;/(void)0;/' ${GAMETILE_SRC}

gametile-mutant-set-always-marks:
	${GAMETILE_MUT} gametile.h \
	    's/if (g->shadow\[i\] != t) {/if (1) {/' ${GAMETILE_SRC}

gametile-mutant-set-unmarked:
	${GAMETILE_MUT} gametile.h \
	    's/^\t\tgtile_mark(g, i);$$/\t\t(void)0;/' ${GAMETILE_SRC}

gametile-mutant-set-tile-range:
	${GAMETILE_MUT} gametile.h \
	    '/^gtile_set/,/^}/s/^\t    t < 0 || t >= LCDG_ATLAS_N)$$/\t    t < 0)/' ${GAMETILE_SRC}

gametile-mutant-set-x-bound:
	${GAMETILE_MUT} gametile.h \
	    's/x < 0 || x >= GTILE_COLS || y < 0 || y >= GTILE_ROWS ||$$/x < 0 || x > GTILE_COLS || y < 0 || y >= GTILE_ROWS ||/' ${GAMETILE_SRC}

gametile-mutant-init-clean:
	${GAMETILE_MUT} gametile.h \
	    's/memset(g->dirty, 0xff, sizeof(g->dirty));$$/memset(g->dirty, 0, sizeof(g->dirty));/' ${GAMETILE_SRC}

gametile-mutant-fill-range:
	${GAMETILE_MUT} gametile.h \
	    's/w > GTILE_COLS - x/w > GTILE_COLS - x + 1/' ${GAMETILE_SRC}

gametile-mutant-fill-tile-range:
	${GAMETILE_MUT} gametile.h \
	    '/^gtile_fill/,/^}/s/^\t    t < 0 || t >= LCDG_ATLAS_N)$$/\t    t < 0)/' ${GAMETILE_SRC}

gametile-mutant-short-write-accepted:
	${GAMETILE_MUT} gametile.h \
	    's/(size_t)run) != run)/(size_t)run) < 0)/' ${GAMETILE_SRC}

gametile-mutant-ioctl-failure-ignored:
	${GAMETILE_MUT} gametile.h \
	    's/GTILE_IOCTL(g->fd, LCDG_TILEWIN, \&w) < 0)/GTILE_IOCTL(g->fd, LCDG_TILEWIN, \&w) < -1)/' ${GAMETILE_SRC}

gametile-mutant-window-height:
	${GAMETILE_MUT} gametile.h \
	    's/w.th = 1;/w.th = 2;/' ${GAMETILE_SRC}

gametile-mutant-write-source:
	${GAMETILE_MUT} gametile.h \
	    's/g->shadow + y \* GTILE_COLS + x,/g->shadow + x,/' ${GAMETILE_SRC}

gametile-mutant-load-last-tile:
	${GAMETILE_MUT} gametile.h \
	    's/count <= LCDG_ATLAS_N - first;/count < LCDG_ATLAS_N - first;/' ${GAMETILE_SRC}

gametile-mutant-load-high:
	${GAMETILE_MUT} gametile.h \
	    's/count <= LCDG_ATLAS_N - first;/count <= LCDG_ATLAS_N - first + 1;/' ${GAMETILE_SRC}

gametile-mutant-load-count-zero:
	${GAMETILE_MUT} gametile.h \
	    's/ count >= 1 \&\&$$//' ${GAMETILE_SRC}

gametile-mutant-load-touch-missing:
	${GAMETILE_MUT} gametile.h \
	    's/^\tgtile_touch_tiles(g, first, count);$$/\t(void)0;/' ${GAMETILE_SRC}

gametile-mutant-touch-range:
	${GAMETILE_MUT} gametile.h \
	    's/g->shadow\[i\] - first < count/g->shadow[i] - first <= count/' ${GAMETILE_SRC}

gametile-mutant-load-bytes:
	${GAMETILE_MUT} gametile.h \
	    's/n = count \* LCDG_TILE_BYTES;/n = count * 16;/' ${GAMETILE_SRC}

gametile-mutant-fill-column-sum:
	${GAMETILE_MUT} gametile.h \
	    's/w > GTILE_COLS - x/x + w > GTILE_COLS/' ${GAMETILE_SRC}

gametile-mutant-fill-row-sum:
	${GAMETILE_MUT} gametile.h \
	    's/h > GTILE_ROWS - y/y + h > GTILE_ROWS/' ${GAMETILE_SRC}

gametile-mutant-fill-origin-unchecked:
	${GAMETILE_MUT} gametile.h \
	    's/x < 0 || x > GTILE_COLS || y < 0 || y > GTILE_ROWS ||$$/0 ||/' ${GAMETILE_SRC}

gametile-mutant-load-sum:
	${GAMETILE_MUT} gametile.h \
	    's/count <= LCDG_ATLAS_N - first;/first + count <= LCDG_ATLAS_N;/' ${GAMETILE_SRC}

gametile-mutant-load-product:
	${GAMETILE_MUT} gametile.h \
	    '/^gtile_load/,/^}/s/^\tif (!gtile_range_ok(first, count))$$/\tif ((n = count * LCDG_TILE_BYTES) < 0 || !gtile_range_ok(first, count))/' ${GAMETILE_SRC}

gametile-mutant-touch-unchecked:
	${GAMETILE_MUT} gametile.h \
	    '/^gtile_touch_tiles/,/^}/s/^\tif (!gtile_range_ok(first, count))$$/\tif (0)/' ${GAMETILE_SRC}

GFX_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${KCSTD}" "${DEV}" \
		lcdgfx_test.c

GFX_SRC=	lcdgfx.c lcdgfx.h lcdterm.c lcdterm.h lcdfont.c lcdfont.h

gfx-mutants: \
	gfx-mutant-exclusive-open \
	gfx-mutant-no-restore \
	gfx-mutant-coord-bound \
	gfx-mutant-rect-order \
	gfx-mutant-circle-right \
	gfx-mutant-circle-left \
	gfx-mutant-window-format \
	gfx-mutant-window-extent \
	gfx-mutant-window-gray \
	gfx-mutant-palette-index \
	gfx-mutant-scroll-range \
	gfx-mutant-scrdef-sum \
	gfx-mutant-scrdef-scroll \
	gfx-mutant-colmod-565 \
	gfx-mutant-colmod-any \
	gfx-mutant-widen-red \
	gfx-mutant-byte-order \
	gfx-mutant-index-mask \
	gfx-mutant-row-padding \
	gfx-mutant-odd-write \
	gfx-mutant-write-overrun \
	gfx-mutant-ioctl-abandon \
	gfx-mutant-close-abandon \
	gfx-mutant-frame-overdraw \
	gfx-mutant-circle-mirror \
	gfx-mutant-circle-swap \
	gfx-mutant-line-error \
	gfx-mutant-line-fast-path \
	gfx-mutant-palette-default \
	gfx-mutant-open-format \
	gfx-mutant-open-scroll \
	gfx-mutant-info-bpp \
	gfx-mutant-gate-cell \
	gfx-mutant-enter-flag \
	gfx-mutant-leave-repaint \
	gfx-mutant-leave-reset \
	gfx-mutant-abort-leave \
	gfx-mutant-tileset-bound-high \
	gfx-mutant-tileset-bound-low \
	gfx-mutant-tileset-count-zero \
	gfx-mutant-tileset-offset \
	gfx-mutant-tileset-overrun \
	gfx-mutant-tileset-abandon \
	gfx-mutant-tilewin-right \
	gfx-mutant-tilewin-bottom \
	gfx-mutant-tilewin-width-zero \
	gfx-mutant-tilewin-height-zero \
	gfx-mutant-tilewin-abandon \
	gfx-mutant-tilewin-restart \
	gfx-mutant-tile-number-bound \
	gfx-mutant-tile-number-continue \
	gfx-mutant-tile-resid-clamp \
	gfx-mutant-tile-excess \
	gfx-mutant-tile-window-right \
	gfx-mutant-tile-row-step \
	gfx-mutant-tile-interleave \
	gfx-mutant-tile-line-stride \
	gfx-mutant-tile-nibble-order \
	gfx-mutant-tile-palette-entry \
	gfx-mutant-tile-open-clear \
	gfx-mutant-info-tiles

gfx-mutant-exclusive-open:
	${GFX_MUT} lcdgfx.c \
	    's/^\tif (gfx_open)$$/\tif (0)/' ${GFX_SRC}

gfx-mutant-no-restore:
	${GFX_MUT} lcdgfx.c \
	    's/return lcdcons_gfx(0);/return 0;/' ${GFX_SRC}

gfx-mutant-coord-bound:
	${GFX_MUT} lcdgfx.c \
	    's/\*v++ >= LCDG_WIDTH/*v++ > LCDG_WIDTH/' ${GFX_SRC}

gfx-mutant-rect-order:
	${GFX_MUT} lcdgfx.c \
	    's/ || rc->x0 > rc->x1//' ${GFX_SRC}

gfx-mutant-circle-right:
	${GFX_MUT} lcdgfx.c \
	    's/ci->x + ci->r >= LCDG_WIDTH/ci->x + ci->r > LCDG_WIDTH/' ${GFX_SRC}

gfx-mutant-circle-left:
	${GFX_MUT} lcdgfx.c \
	    's/ci->r > ci->x || //' ${GFX_SRC}

gfx-mutant-window-format:
	${GFX_MUT} lcdgfx.c \
	    's/w->fmt > LCDG_WIN_BITS/w->fmt > 3/' ${GFX_SRC}

gfx-mutant-window-extent:
	${GFX_MUT} lcdgfx.c \
	    's/w->x + w->w > LCDG_WIDTH/w->x + w->w > LCDG_WIDTH + 1/' ${GFX_SRC}

gfx-mutant-window-gray:
	${GFX_MUT} lcdgfx.c \
	    's/w->fg >= LCDG_NPAL || //' ${GFX_SRC}

gfx-mutant-palette-index:
	${GFX_MUT} lcdgfx.c \
	    's/pl->index >= LCDG_NPAL/pl->index > LCDG_NPAL/' ${GFX_SRC}

gfx-mutant-scroll-range:
	${GFX_MUT} lcdgfx.c \
	    's/sc->line >= LCDG_RAM_LINES - scr_bottom/sc->line > LCDG_RAM_LINES - scr_bottom/' ${GFX_SRC}

gfx-mutant-scrdef-sum:
	${GFX_MUT} lcdgfx.c \
	    's/sd->top + sd->bottom >= LCDG_RAM_LINES/sd->top + sd->bottom > LCDG_RAM_LINES/' ${GFX_SRC}

gfx-mutant-scrdef-scroll:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\tlcd_gfx_scroll(scr_top);/\t\t(void)0;/' ${GFX_SRC}

gfx-mutant-colmod-565:
	${GFX_MUT} lcdgfx.c \
	    's/ \&\& cm->fmt != LCDG_COLMOD_RGB565//' ${GFX_SRC}

gfx-mutant-colmod-any:
	${GFX_MUT} lcdgfx.c \
	    's/if (cm->fmt != LCDG_COLMOD_RGB666 \&\& cm->fmt != LCDG_COLMOD_RGB565)/if (0)/' ${GFX_SRC}

gfx-mutant-widen-red:
	${GFX_MUT} lcdgfx.c \
	    's/lcd_gfx_byte(r << 3 | r >> 2);/lcd_gfx_byte(r << 3);/' ${GFX_SRC}

gfx-mutant-byte-order:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\tlcd_gfx_byte(c >> 8);/\t\tlcd_gfx_byte(c \& 0xff);/' ${GFX_SRC}

gfx-mutant-index-mask:
	${GFX_MUT} lcdgfx.c \
	    's/gfx_pal\[v \& (LCDG_NPAL - 1)\]/gfx_pal[v % 17]/' ${GFX_SRC}

gfx-mutant-row-padding:
	${GFX_MUT} lcdgfx.c \
	    's/win_col += 8;/win_col += 1;/' ${GFX_SRC}

gfx-mutant-odd-write:
	${GFX_MUT} lcdgfx.c \
	    's/(uio->uio_resid \& 1) != 0/0/' ${GFX_SRC}

gfx-mutant-write-overrun:
	${GFX_MUT} lcdgfx.c \
	    's/if (n > win_left)/if (0)/' ${GFX_SRC}

gfx-mutant-ioctl-abandon:
	${GFX_MUT} lcdgfx.c \
	    '/^lcdgfx_ioctl/,/^}/s/^\twindow_end();/\t(void)0;/' ${GFX_SRC}

gfx-mutant-close-abandon:
	${GFX_MUT} lcdgfx.c \
	    '/^lcdgfx_close/,/^}/s/^\twindow_end();/\t(void)0;/' ${GFX_SRC}

gfx-mutant-frame-overdraw:
	${GFX_MUT} lcdgfx.c \
	    's/if (y1 - y0 > 1) {/if (y1 - y0 > 0) {/' ${GFX_SRC}

gfx-mutant-circle-mirror:
	${GFX_MUT} lcdgfx.c \
	    's/(i \& 2 ? -dy : dy)/dy/' ${GFX_SRC}

gfx-mutant-circle-swap:
	${GFX_MUT} lcdgfx.c \
	    's/dx = i \& 4 ? y : x;/dx = x;/' ${GFX_SRC}

gfx-mutant-line-error:
	${GFX_MUT} lcdgfx.c \
	    's/e2 = 2 \* err;/e2 = err;/' ${GFX_SRC}

gfx-mutant-line-fast-path:
	${GFX_MUT} lcdgfx.c \
	    's/if (x0 == x1 || y0 == y1) {/if (0) {/' ${GFX_SRC}

gfx-mutant-palette-default:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\tgfx_pal\[i\] = rgb565(/\t\tgfx_pal[i] = 0 \& rgb565(/' ${GFX_SRC}

gfx-mutant-open-format:
	${GFX_MUT} lcdgfx.c \
	    's/^\tgfx_fmt = LCDG_COLMOD_RGB666;/\t(void)0;/' ${GFX_SRC}

gfx-mutant-open-scroll:
	${GFX_MUT} lcdgfx.c \
	    's/^\tlcd_gfx_scroll(0);/\t(void)0;/' ${GFX_SRC}

gfx-mutant-info-bpp:
	${GFX_MUT} lcdgfx.c \
	    's/gfx_fmt == LCDG_COLMOD_RGB565 ? 16 : 18/18/' ${GFX_SRC}

gfx-mutant-gate-cell:
	${GFX_MUT} lcdterm.c \
	    '/^panel_cell/,/^}/s/if (!gfx_active)/if (1)/' ${GFX_SRC}

gfx-mutant-enter-flag:
	${GFX_MUT} lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/gfx_active = on;/gfx_active = 0;/' ${GFX_SRC}

gfx-mutant-leave-repaint:
	${GFX_MUT} lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/lcdterm_repaint();/(void)0;/' ${GFX_SRC}

gfx-mutant-leave-reset:
	${GFX_MUT} lcdterm.c \
	    '/^lcdterm_gfx/,/^}/s/lcd_gfx_reset();/(void)0;/' ${GFX_SRC}

gfx-mutant-abort-leave:
	${GFX_MUT} lcdterm.c \
	    '/^lcdterm_abort/,/^}/s/lcdterm_gfx(0);/lcd_set_top(top);/' ${GFX_SRC}

gfx-mutant-tileset-bound-high:
	${GFX_MUT} lcdgfx.c \
	    's/ts->first + ts->count > LCDG_ATLAS_N/ts->first + ts->count > LCDG_ATLAS_N + 1/' ${GFX_SRC}

gfx-mutant-tileset-bound-low:
	${GFX_MUT} lcdgfx.c \
	    's/ts->first + ts->count > LCDG_ATLAS_N/ts->first + ts->count >= LCDG_ATLAS_N/' ${GFX_SRC}

gfx-mutant-tileset-count-zero:
	${GFX_MUT} lcdgfx.c \
	    's/ts->count == 0 || //' ${GFX_SRC}

gfx-mutant-tileset-offset:
	${GFX_MUT} lcdgfx.c \
	    's/tset_pos = ts->first \* LCDG_TILE_BYTES;/tset_pos = 0;/' ${GFX_SRC}

gfx-mutant-tileset-overrun:
	${GFX_MUT} lcdgfx.c \
	    's/if (n > tset_left)/if (0)/' ${GFX_SRC}

gfx-mutant-tileset-abandon:
	${GFX_MUT} lcdgfx.c \
	    '/^window_end/,/^}/s/^\ttset_left = 0;$$/\t(void)0;/' ${GFX_SRC}

gfx-mutant-tilewin-right:
	${GFX_MUT} lcdgfx.c \
	    's/tl->tx + tl->tw > LCDG_GRID/tl->tx + tl->tw > LCDG_GRID + 1/' ${GFX_SRC}

gfx-mutant-tilewin-bottom:
	${GFX_MUT} lcdgfx.c \
	    's/tl->ty + tl->th > LCDG_GRID/tl->ty + tl->th > LCDG_GRID + 1/' ${GFX_SRC}

gfx-mutant-tilewin-width-zero:
	${GFX_MUT} lcdgfx.c \
	    's/tl->tw == 0 || //' ${GFX_SRC}

gfx-mutant-tilewin-height-zero:
	${GFX_MUT} lcdgfx.c \
	    's/ tl->th == 0 ||$$//' ${GFX_SRC}

gfx-mutant-tilewin-abandon:
	${GFX_MUT} lcdgfx.c \
	    '/^window_end/,/^}/s/^\ttw_left = 0;$$/\t(void)0;/' ${GFX_SRC}

gfx-mutant-tilewin-restart:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\ttw_fill = 0;$$/\t\t(void)0;/' ${GFX_SRC}

gfx-mutant-tile-number-bound:
	${GFX_MUT} lcdgfx.c \
	    's/tw_row\[i\] >= LCDG_ATLAS_N/tw_row[i] > LCDG_ATLAS_N/' ${GFX_SRC}

gfx-mutant-tile-number-continue:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\t\t\ttw_left = 0;$$/\t\t\t\t(void)0;/' ${GFX_SRC}

gfx-mutant-tile-resid-clamp:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\tif (n > uio->uio_resid)$$/\t\tif (0)/' ${GFX_SRC}

gfx-mutant-tile-excess:
	${GFX_MUT} lcdgfx.c \
	    's/uio->uio_resid > 0 \&\& tw_left > 0/uio->uio_resid > 0/' ${GFX_SRC}

gfx-mutant-tile-window-right:
	${GFX_MUT} lcdgfx.c \
	    's/(tw_x + tw_w) \* LCDG_TILE_W - 1/(tw_x + tw_w) * LCDG_TILE_W - 2/' ${GFX_SRC}

gfx-mutant-tile-row-step:
	${GFX_MUT} lcdgfx.c \
	    's/^\t\t\ttw_y++;$$/\t\t\t(void)0;/' ${GFX_SRC}

gfx-mutant-tile-interleave:
	${GFX_MUT} lcdgfx.c \
	    '/^\tfor (line = 0; line < LCDG_TILE_H; line++) {$$/{s/line = 0; line < LCDG_TILE_H; line++/i = 0; i < tw_w; i++/;n;s/i = 0; i < tw_w; i++/line = 0; line < LCDG_TILE_H; line++/;}' ${GFX_SRC}

gfx-mutant-tile-line-stride:
	${GFX_MUT} lcdgfx.c \
	    's/line \* 4/line * 3/' ${GFX_SRC}

gfx-mutant-tile-nibble-order:
	${GFX_MUT} lcdgfx.c \
	    '/put_color(gfx_pal\[p\[b\] >> 4\]);/{s/>> 4/\& 15/;n;s/\& 15/>> 4/;}' ${GFX_SRC}

gfx-mutant-tile-palette-entry:
	${GFX_MUT} lcdgfx.c \
	    's/gfx_pal\[p\[b\] >> 4\]/gfx_pal[(p[b] >> 4) \& 7]/' ${GFX_SRC}

gfx-mutant-tile-open-clear:
	${GFX_MUT} lcdgfx.c \
	    's/^\tbzero(atlas, sizeof(atlas));$$/\t(void)0;/' ${GFX_SRC}

gfx-mutant-info-tiles:
	${GFX_MUT} lcdgfx.c \
	    's/in->tiles = LCDG_ATLAS_N;/in->tiles = 0;/' ${GFX_SRC}

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

SDSPI_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" \
		"${CSTD} -idirafter ${TOPSRC}/sys" "${DEV}" sdspi_test.c

SDSPI_SRC=	sdspi.c sdspi.h ${CRC32}

sdspi-mutants: \
	sdspi-mutant-crc-cmd8 \
	sdspi-mutant-crc-cmd0 \
	sdspi-mutant-hcs \
	sdspi-mutant-byte-address \
	sdspi-mutant-echo-pattern \
	sdspi-mutant-init-timeout \
	sdspi-mutant-write-response \
	sdspi-mutant-csd-v2-shift \
	sdspi-mutant-csd-v1-mult \
	sdspi-mutant-ccs \
	sdspi-mutant-blocklen \
	sdspi-mutant-fast-speed \
	sdspi-mutant-busy-timeout \
	sdspi-mutant-start-token \
	sdspi-mutant-read-timeout \
	sdspi-mutant-powerup-clocks \
	sdspi-mutant-cmd0-tries \
	sdspi-mutant-ocr-ready \
	sdspi-mutant-acmd-illegal \
	sdspi-mutant-range-check \
	sdspi-mutant-span-window \
	sdspi-mutant-span-padding \
	sdspi-mutant-span-sum-late

sdspi-mutant-crc-cmd8:
	${SDSPI_MUT} sdspi.c \
	    's/cmd == CMD8 ? 0x87/cmd == CMD8 ? 0x86/' ${SDSPI_SRC}

sdspi-mutant-crc-cmd0:
	${SDSPI_MUT} sdspi.c \
	    's/cmd == CMD0 ? 0x95/cmd == CMD0 ? 0x94/' ${SDSPI_SRC}

sdspi-mutant-hcs:
	${SDSPI_MUT} sdspi.c \
	    's/hcs = v2 ? 0x40000000 : 0;/hcs = 0;/' ${SDSPI_SRC}

sdspi-mutant-byte-address:
	${SDSPI_MUT} sdspi.c \
	    's/sd->type == SD_HC ? sector : sector \* SD_SECTOR/sd->type == SD_HC ? sector : sector/' ${SDSPI_SRC}

sdspi-mutant-echo-pattern:
	${SDSPI_MUT} sdspi.c \
	    's/echo\[3\] != 0xaa/0/' ${SDSPI_SRC}

sdspi-mutant-init-timeout:
	${SDSPI_MUT} sdspi.c \
	    's/o->micros() - t0 >= INIT_US/(o->micros() - t0) > 0xfffffff0U/' ${SDSPI_SRC}

sdspi-mutant-write-response:
	${SDSPI_MUT} sdspi.c \
	    's/== DATA_ACCEPTED/< DATA_RESP_MASK/' ${SDSPI_SRC}

sdspi-mutant-csd-v2-shift:
	${SDSPI_MUT} sdspi.c \
	    's/return (c_size + 1) << 10;/return (c_size + 1) << 9;/' ${SDSPI_SRC}

sdspi-mutant-csd-v1-mult:
	${SDSPI_MUT} sdspi.c \
	    's/(mult + 2 + len - 9)/(mult + 1 + len - 9)/' ${SDSPI_SRC}

sdspi-mutant-ccs:
	${SDSPI_MUT} sdspi.c \
	    's/(ocr\[0\] & 0x40) ? SD_HC/(ocr[0] \& 0x80) ? SD_HC/' ${SDSPI_SRC}

sdspi-mutant-blocklen:
	${SDSPI_MUT} sdspi.c \
	    's/if (sd->type != SD_HC) {/if (0) {/' ${SDSPI_SRC}

sdspi-mutant-fast-speed:
	${SDSPI_MUT} sdspi.c \
	    's/o->speed(1);/(void)0;/' ${SDSPI_SRC}

sdspi-mutant-busy-timeout:
	${SDSPI_MUT} sdspi.c \
	    's/o->micros() - t0 >= BUSY_US) {/(o->micros() - t0) > 0xfffffff0U) {/' ${SDSPI_SRC}

sdspi-mutant-start-token:
	${SDSPI_MUT} sdspi.c \
	    's/if (b != TOKEN_START)/if (0)/' ${SDSPI_SRC}

sdspi-mutant-read-timeout:
	${SDSPI_MUT} sdspi.c \
	    's/o->micros() - t0 >= READ_US/(o->micros() - t0) > 0xfffffff0U/' ${SDSPI_SRC}

sdspi-mutant-powerup-clocks:
	${SDSPI_MUT} sdspi.c \
	    's/POWERUP_CLOCKS\t\t10/POWERUP_CLOCKS\t\t2/' ${SDSPI_SRC}

sdspi-mutant-cmd0-tries:
	${SDSPI_MUT} sdspi.c \
	    's/CMD0_TRIES\t\t5/CMD0_TRIES\t\t1/' ${SDSPI_SRC}

sdspi-mutant-ocr-ready:
	${SDSPI_MUT} sdspi.c \
	    's/!(ocr\[0\] & 0x80)/0/' ${SDSPI_SRC}

sdspi-mutant-acmd-illegal:
	${SDSPI_MUT} sdspi.c \
	    's/return (r & R1_ILLEGAL) ? SDE_UNSUPPORTED : SDE_IO;/return SDE_IO;/' ${SDSPI_SRC}

sdspi-mutant-range-check:
	${SDSPI_MUT} sdspi.c \
	    's/ || sector >= sd->nsectors//' ${SDSPI_SRC}

sdspi-mutant-span-window:
	${SDSPI_MUT} sdspi.c \
	    's/if (i - off < len)/if (i - off < len + 1)/' ${SDSPI_SRC}

sdspi-mutant-span-padding:
	${SDSPI_MUT} sdspi.c \
	    's/b = i < len ? buf\[i\] : 0;/b = i < len ? buf[i] : 0xff;/' ${SDSPI_SRC}

sdspi-mutant-span-sum-late:
	${SDSPI_MUT} sdspi.c \
	    's/\*crc = crc32_update(\*crc, &b, 1);/*crc = crc32_update(*crc, \&buf[i], 1);/' ${SDSPI_SRC}


SDSTRAT_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" \
		"${KCSTD} -idirafter ${TOPSRC}/sys" "${DEV}" sdstrat_test.c

# The driver, the dump stream it writes through and the CRC; a mutant of
# dump.c names it relative to DEV.
SDSTRAT_SRC=	sd.c ../rp2040/dump.c ${CRC32}

sdstrat-mutants: \
	sdstrat-mutant-whole-block-only \
	sdstrat-mutant-stale-resid \
	sdstrat-mutant-truncation-resid \
	sdstrat-mutant-truncation-clamp \
	sdstrat-mutant-eof-resid \
	sdstrat-mutant-eof-past \
	sdstrat-mutant-odd-tail \
	sdstrat-mutant-partition-offset \
	sdstrat-mutant-block-units \
	sdstrat-mutant-direction \
	sdstrat-mutant-card-changed \
	sdstrat-mutant-open-check \
	sdstrat-mutant-reinit-early-clear \
	sdstrat-mutant-reinit-keep-old \
	sdstrat-mutant-raw-read-unchecked \
	sdstrat-mutant-raw-write-unchecked \
	sdstrat-mutant-raw-offset-unchecked \
	sdstrat-mutant-raw-sector-granularity \
	sdstrat-mutant-raw-segments-unchecked \
	sdstrat-mutant-reject-resid \
	sdstrat-mutant-failure-resid \
	sdstrat-mutant-failure-resid-off-by-one \
	sdstrat-mutant-failure-resid-truncation \
	sdstrat-mutant-sector-replaced \
	sdstrat-mutant-replaced-errno \
	sdstrat-mutant-reinit-held \
	sdstrat-mutant-reinit-held-raw \
	sdstrat-mutant-reinit-replaced \
	sdstrat-mutant-debounce-latest \
	sdstrat-mutant-debounce-interval \
	sdstrat-mutant-debounce-boundary \
	sdstrat-mutant-debounce-restart \
	sdstrat-mutant-debounce-pending \
	sdstrat-mutant-accept-candidate \
	sdstrat-mutant-accept-committed \
	sdstrat-mutant-accept-flag \
	sdstrat-mutant-spl-dropped \
	sdstrat-mutant-media-gone-constant \
	sdstrat-mutant-media-gone-inverted \
	sdstrat-mutant-getpart-whole \
	sdstrat-mutant-probe-ungated \
	sdstrat-mutant-probe-never \
	sdstrat-mutant-dump-busy-unchecked \
	sdstrat-mutant-dump-replaced-ignored \
	sdstrat-mutant-dump-overlap-adjacent \
	sdstrat-mutant-dump-type-ignored \
	sdstrat-mutant-dump-past-card \
	sdstrat-mutant-dump-signature-unchecked \
	sdstrat-mutant-dump-probe-unarmed \
	sdstrat-mutant-dump-device-unchecked \
	sdstrat-mutant-dump-offset-ignored \
	sdstrat-mutant-dump-no-invalidate \
	sdstrat-mutant-dump-sum-dropped \
	sdstrat-mutant-dump-size-unchecked \
	sdstrat-mutant-dump-length-short \
	sdstrat-mutant-dump-table-dropped

sdstrat-mutant-whole-block-only:
	${SDSTRAT_MUT} sd.c \
	    's/(bp->b_bcount \& (SD_SECTOR - 1))/(bp->b_bcount \& (DEV_BSIZE - 1))/' ${SDSTRAT_SRC}

sdstrat-mutant-stale-resid:
	${SDSTRAT_MUT} sd.c \
	    's/^\tbp->b_resid = 0;/\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-truncation-resid:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid = (n - avail) \* SD_SECTOR;/(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-truncation-clamp:
	${SDSTRAT_MUT} sd.c \
	    's/if (n > avail) {/if (0) {/' ${SDSTRAT_SRC}

sdstrat-mutant-eof-resid:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\t\tbp->b_resid = bp->b_bcount;/\t\t\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-eof-past:
	${SDSTRAT_MUT} sd.c \
	    's/if ((u_int)bp->b_blkno == blocks) {/if ((u_int)bp->b_blkno >= blocks) {/' ${SDSTRAT_SRC}

sdstrat-mutant-odd-tail:
	${SDSTRAT_MUT} sd.c \
	    's/(p->dp_nsectors + 1) >> 1;/p->dp_nsectors >> 1;/' ${SDSTRAT_SRC}

sdstrat-mutant-partition-offset:
	${SDSTRAT_MUT} sd.c \
	    's/sector = p->dp_offset + first;/sector = first;/' ${SDSTRAT_SRC}

sdstrat-mutant-block-units:
	${SDSTRAT_MUT} sd.c \
	    's/(u_int)bp->b_blkno \* (DEV_BSIZE \/ SD_SECTOR)/(u_int)bp->b_blkno/' ${SDSTRAT_SRC}

sdstrat-mutant-direction:
	${SDSTRAT_MUT} sd.c \
	    's/((bp->b_flags \& B_READ) ?/((bp->b_flags \& B_READ) == 0 ?/' ${SDSTRAT_SRC}

sdstrat-mutant-card-changed:
	${SDSTRAT_MUT} sd.c \
	    's/changed = sd_replaced();/changed = 0;/' ${SDSTRAT_SRC}

sdstrat-mutant-open-check:
	${SDSTRAT_MUT} sd.c \
	    's/(sd_openpart \& (1U << part)) == 0) {/0) {/' ${SDSTRAT_SRC}

sdstrat-mutant-reinit-early-clear:
	${SDSTRAT_MUT} sd.c \
	    's/^\tbzero(pt, sizeof(pt));/\tbzero(pt, sizeof(pt)); bzero(sdpt, sizeof(sdpt));/' ${SDSTRAT_SRC}

sdstrat-mutant-reinit-keep-old:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\tbcopy(pt, sdpt, sizeof(sdpt));/\t\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-raw-read-unchecked:
	${SDSTRAT_MUT} sd.c \
	    '/^sdread(/,/^}/s/sd_misaligned(uio)/0/' ${SDSTRAT_SRC}

sdstrat-mutant-raw-write-unchecked:
	${SDSTRAT_MUT} sd.c \
	    '/^sdwrite(/,/^}/s/sd_misaligned(uio)/0/' ${SDSTRAT_SRC}

sdstrat-mutant-raw-offset-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_offset \& (DEV_BSIZE - 1)) != 0/0/' ${SDSTRAT_SRC}

sdstrat-mutant-raw-sector-granularity:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_offset \& (DEV_BSIZE - 1))/(uio->uio_offset \& (SD_SECTOR - 1))/' ${SDSTRAT_SRC}

sdstrat-mutant-raw-segments-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_iov\[i\].iov_len \& (DEV_BSIZE - 1)) != 0/0/' ${SDSTRAT_SRC}


sdstrat-mutant-reject-resid:
	${SDSTRAT_MUT} sd.c \
	    's/^\tbp->b_resid = bp->b_bcount;/\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-failure-resid:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid += (n - i) \* SD_SECTOR;/(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-failure-resid-off-by-one:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid += (n - i) \* SD_SECTOR;/bp->b_resid += (n - i - 1) * SD_SECTOR;/' ${SDSTRAT_SRC}

sdstrat-mutant-failure-resid-truncation:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid += (n - i) \* SD_SECTOR;/bp->b_resid = (n - i) * SD_SECTOR;/' ${SDSTRAT_SRC}

sdstrat-mutant-sector-replaced:
	${SDSTRAT_MUT} sd.c \
	    's/if (sd_replaced() ||$$/if (0 ||/' ${SDSTRAT_SRC}

sdstrat-mutant-replaced-errno:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_error = changed ? EIO : EINVAL;/bp->b_error = EINVAL;/' ${SDSTRAT_SRC}

sdstrat-mutant-reinit-held:
	${SDSTRAT_MUT} sd.c \
	    's/if (sd_blkopen != 0)/if (0)/' ${SDSTRAT_SRC}

sdstrat-mutant-reinit-held-raw:
	${SDSTRAT_MUT} sd.c \
	    's/if (sd_blkopen != 0)/if (sd_openpart != 0)/' ${SDSTRAT_SRC}

sdstrat-mutant-reinit-replaced:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\tif (sd_replaced())$$/\t\tif (0)/' ${SDSTRAT_SRC}

sdstrat-mutant-debounce-latest:
	${SDSTRAT_MUT} sd.c \
	    's/if (level != det_cand) {/if (level != det_level) {/' ${SDSTRAT_SRC}

sdstrat-mutant-debounce-interval:
	${SDSTRAT_MUT} sd.c \
	    's/pc_micros() - det_t0 >= SD_DET_DEBOUNCE_US/1/' ${SDSTRAT_SRC}

sdstrat-mutant-debounce-boundary:
	${SDSTRAT_MUT} sd.c \
	    's/det_t0 >= SD_DET_DEBOUNCE_US/det_t0 > SD_DET_DEBOUNCE_US/' ${SDSTRAT_SRC}

sdstrat-mutant-debounce-restart:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\tdet_t0 = pc_micros();/\t\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-debounce-pending:
	${SDSTRAT_MUT} sd.c \
	    's/else if (det_cand != det_level \&\&/else if (/' ${SDSTRAT_SRC}

sdstrat-mutant-accept-candidate:
	${SDSTRAT_MUT} sd.c \
	    's/^\tdet_cand = det_level;$$/\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-accept-committed:
	${SDSTRAT_MUT} sd.c \
	    's/^\tdet_level = sd_det_pin();$$/\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-accept-flag:
	${SDSTRAT_MUT} sd.c \
	    's/^\tcard_changed = 0;$$/\t(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-spl-dropped:
	${SDSTRAT_MUT} sd.c \
	    's/^\tint s = splbio();$$/\tint s = 0;/' ${SDSTRAT_SRC}


sdstrat-mutant-media-gone-constant:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\t\*(int \*)addr = sd_replaced();/\t\t*(int *)addr = 0;/' ${SDSTRAT_SRC}

sdstrat-mutant-media-gone-inverted:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\t\*(int \*)addr = sd_replaced();/\t\t*(int *)addr = !sd_replaced();/' ${SDSTRAT_SRC}

sdstrat-mutant-getpart-whole:
	${SDSTRAT_MUT} sd.c \
	    's/\*(struct diskpart \*)addr = sdpt\[part\];/*(struct diskpart *)addr = sdpt[0];/' ${SDSTRAT_SRC}

sdstrat-mutant-probe-ungated:
	${SDSTRAT_MUT} sd.c \
	    's/^\tif (board_id() != BOARD_ID_PICOCALC)$$/\tif (0)/' ${SDSTRAT_SRC}

sdstrat-mutant-probe-never:
	${SDSTRAT_MUT} sd.c \
	    's/^\tif (board_id() != BOARD_ID_PICOCALC)$$/\tif (1)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-busy-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/if (sd_dump_busy(dp.dp_offset, dp.dp_nsectors)) {/if (sd_dump_busy(dp.dp_offset, dp.dp_nsectors) \&\& 0) {/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-replaced-ignored:
	${SDSTRAT_MUT} sd.c \
	    's/if (card_changed)/if (0)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-overlap-adjacent:
	${SDSTRAT_MUT} sd.c \
	    's/ \&\& offset < end)/ \&\& offset <= end)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-type-ignored:
	${SDSTRAT_MUT} sd.c \
	    's/if (dp.dp_type == DUMP_PART_TYPE \&\& dp.dp_nsectors != 0 \&\&/if (dp.dp_nsectors != 0 \&\&/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-past-card:
	${SDSTRAT_MUT} sd.c \
	    's/dp.dp_nsectors <= dd.card.nsectors - dp.dp_offset)/1)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-signature-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/tab\[sizeof(tab) - 1\] != 0xaa) {/0) {/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-probe-unarmed:
	${SDSTRAT_MUT} sd.c \
	    's/dumpdev = makedev(SD_BMAJOR, 0);/(void)0;/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-device-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/if (dev != makedev(SD_BMAJOR, 0))/if (dev == (dev_t)-2)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-offset-ignored:
	${SDSTRAT_MUT} sd.c \
	    's/dd.offset = dp.dp_offset;/dd.offset = 0;/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-no-invalidate:
	${SDSTRAT_MUT} ../rp2040/dump.c \
	    's/if (put(arg, 0, (const unsigned char \*)"", 0, (unsigned int \*)0) != 0)/if (0)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-sum-dropped:
	${SDSTRAT_MUT} ../rp2040/dump.c \
	    's/if (put(arg, sector++, p, len, \&crc) != 0)/if (put(arg, sector++, p, len, (unsigned int *)0) != 0)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-size-unchecked:
	${SDSTRAT_MUT} ../rp2040/dump.c \
	    's/if (need > nsectors)/if (need > nsectors + 4)/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-length-short:
	${SDSTRAT_MUT} ../rp2040/dump.c \
	    's/sect0.h.dh_bytes = sector \* DUMP_SECTOR;/sect0.h.dh_bytes = (sector - 1) * DUMP_SECTOR;/' ${SDSTRAT_SRC}

sdstrat-mutant-dump-table-dropped:
	${SDSTRAT_MUT} ../rp2040/dump.c \
	    's/len = sizeof(sect0.h) + nspan \* sizeof(sect0.s\[0\]);/len = sizeof(sect0.h);/' ${SDSTRAT_SRC}


PICOAUDIO_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${KCSTD}" "${DEV}" \
		picoaudio_test.c

picoaudio-mutants: \
	picoaudio-mutant-left-override \
	picoaudio-mutant-right-override \
	picoaudio-mutant-override-high \
	picoaudio-mutant-slice-left-on \
	picoaudio-mutant-override-kept \
	picoaudio-mutant-busy-kept

picoaudio-mutant-left-override:
	${PICOAUDIO_MUT} picoaudio.c \
	    '/PC_IO_BANK0_CTRL(PICOCALC_AUDIO_L)) =$$/{n;s/ | GPIO_OUTOVER_LOW//;}' picoaudio.c

picoaudio-mutant-right-override:
	${PICOAUDIO_MUT} picoaudio.c \
	    '/PC_IO_BANK0_CTRL(PICOCALC_AUDIO_R)) =$$/{n;s/ | GPIO_OUTOVER_LOW//;}' picoaudio.c

picoaudio-mutant-override-high:
	${PICOAUDIO_MUT} picoaudio.c \
	    's/(2UL << 8)/(3UL << 8)/' picoaudio.c

picoaudio-mutant-slice-left-on:
	${PICOAUDIO_MUT} picoaudio.c \
	    's/PWM_REG(PWM_CSR) = 0;/(void)0;/' picoaudio.c

picoaudio-mutant-override-kept:
	${PICOAUDIO_MUT} picoaudio.c \
	    's/^\tpc_pin(PICOCALC_AUDIO_L, PC_FUNC_PWM, 0);$$/\t(void)0;/' picoaudio.c

picoaudio-mutant-busy-kept:
	${PICOAUDIO_MUT} picoaudio.c \
	    's/bell_on = 0;$$/(void)0;/' picoaudio.c

clean:
	rm -f lcdterm_test lcdgfx_test kbdreq_test kbdevq_test gametile_test \
	    sdspi_test sdstrat_test dump_test picoaudio_test

.PHONY: all check font-sync mutants mutant-scroll mutant-region \
	mutant-ctrl mutant-reverse-erase mutant-bright mutant-bell \
	mutant-bell-any mutant-bank-store mutant-bank-redraw \
	mutant-bank-ascii mutant-bank-c1 mutant-bank-acs-shift \
	mutant-bank-ris mutant-bank-rows mutant-bank-code clean \
	mutant-gfx-gate-cell mutant-gfx-gate-fill mutant-gfx-gate-top \
	mutant-gfx-repaint-row mutant-gfx-repaint-top \
	mutant-gfx-repaint-cursor mutant-gfx-leave-repaint \
	mutant-gfx-leave-reset mutant-gfx-abort \
	kbdreq-mutants kbdreq-mutant-write-bit kbdreq-mutant-echo \
	kbdreq-mutant-retry kbdreq-mutant-expire-sent \
	kbdreq-mutant-percent kbdreq-mutant-level kbdreq-mutant-off-floor \
	kbdreq-mutant-off-ceiling

.PHONY: sdspi-mutants sdspi-mutant-crc-cmd8 \
	sdspi-mutant-crc-cmd0 \
	sdspi-mutant-hcs \
	sdspi-mutant-byte-address \
	sdspi-mutant-echo-pattern \
	sdspi-mutant-init-timeout \
	sdspi-mutant-write-response \
	sdspi-mutant-csd-v2-shift \
	sdspi-mutant-csd-v1-mult \
	sdspi-mutant-ccs \
	sdspi-mutant-blocklen \
	sdspi-mutant-fast-speed \
	sdspi-mutant-busy-timeout \
	sdspi-mutant-start-token \
	sdspi-mutant-read-timeout \
	sdspi-mutant-powerup-clocks \
	sdspi-mutant-cmd0-tries \
	sdspi-mutant-ocr-ready \
	sdspi-mutant-acmd-illegal \
	sdspi-mutant-range-check

.PHONY: sdstrat-mutants \
	sdstrat-mutant-whole-block-only \
	sdstrat-mutant-stale-resid \
	sdstrat-mutant-truncation-resid \
	sdstrat-mutant-truncation-clamp \
	sdstrat-mutant-eof-resid \
	sdstrat-mutant-eof-past \
	sdstrat-mutant-odd-tail \
	sdstrat-mutant-partition-offset \
	sdstrat-mutant-block-units \
	sdstrat-mutant-direction \
	sdstrat-mutant-card-changed \
	sdstrat-mutant-open-check \
	sdstrat-mutant-reinit-early-clear \
	sdstrat-mutant-reinit-keep-old \
	sdstrat-mutant-raw-read-unchecked \
	sdstrat-mutant-raw-write-unchecked \
	sdstrat-mutant-raw-offset-unchecked \
	sdstrat-mutant-raw-sector-granularity \
	sdstrat-mutant-raw-segments-unchecked \
	sdstrat-mutant-reject-resid \
	sdstrat-mutant-failure-resid \
	sdstrat-mutant-failure-resid-off-by-one \
	sdstrat-mutant-failure-resid-truncation \
	sdstrat-mutant-sector-replaced \
	sdstrat-mutant-replaced-errno \
	sdstrat-mutant-reinit-held \
	sdstrat-mutant-reinit-held-raw \
	sdstrat-mutant-reinit-replaced \
	sdstrat-mutant-debounce-latest \
	sdstrat-mutant-debounce-interval \
	sdstrat-mutant-debounce-boundary \
	sdstrat-mutant-debounce-restart \
	sdstrat-mutant-debounce-pending \
	sdstrat-mutant-accept-candidate \
	sdstrat-mutant-accept-committed \
	sdstrat-mutant-accept-flag \
	sdstrat-mutant-spl-dropped \
	sdstrat-mutant-media-gone-constant \
	sdstrat-mutant-media-gone-inverted \
	sdstrat-mutant-getpart-whole \
	sdstrat-mutant-probe-ungated \
	sdstrat-mutant-probe-never

.PHONY: picoaudio-mutants \
	picoaudio-mutant-left-override \
	picoaudio-mutant-right-override \
	picoaudio-mutant-override-high \
	picoaudio-mutant-slice-left-on \
	picoaudio-mutant-override-kept \
	picoaudio-mutant-busy-kept

.PHONY: kbdevq-mutants kbdevq-mutant-check-ignores-group kbdevq-mutant-check-ignores-mode kbdevq-mutant-mode-counts-line-discipline kbdevq-mutant-start-cooked kbdevq-mutant-start-raw-only kbdevq-mutant-start-no-snapshot kbdevq-mutant-modeset-ignored kbdevq-mutant-modeset-owner kbdevq-mutant-drain-any-process kbdevq-mutant-stop-any-process kbdevq-mutant-start-any-process kbdevq-mutant-procexit-ignored kbdevq-mutant-procexit-any-process kbdevq-mutant-owner-pid-ignored kbdevq-mutant-owner-proc-ignored kbdevq-mutant-end-keeps-bytes kbdevq-mutant-put-ungated kbdevq-mutant-drain-unreadable kbdevq-mutant-drain-background kbdevq-mutant-drain-no-session kbdevq-mutant-start-unreadable kbdevq-mutant-start-background kbdevq-mutant-start-keeps-queue kbdevq-mutant-start-steals kbdevq-mutant-stop-any-file kbdevq-mutant-fileclose-ignored kbdevq-mutant-fileclose-any-file kbdevq-mutant-check-ignored kbdevq-mutant-check-always-ends kbdevq-mutant-end-keeps-queue kbdevq-mutant-end-keeps-drops kbdevq-mutant-drop-newest kbdevq-mutant-lost-uncounted kbdevq-mutant-lost-wraps kbdevq-mutant-lost-kept kbdevq-mutant-drain-limit kbdevq-mutant-release-dropped kbdevq-mutant-hold-dropped kbdevq-mutant-idle-queued kbdevq-mutant-key-masked kbdevq-mutant-translate-dropped kbdevq-mutant-drain-slot kbdevq-mutant-put-slot kbdevq-mutant-full-threshold kbdevq-mutant-wiring

.PHONY: gametile-mutants gametile-mutant-fill-column-sum gametile-mutant-fill-row-sum gametile-mutant-fill-origin-unchecked gametile-mutant-load-sum gametile-mutant-load-product gametile-mutant-touch-unchecked gametile-mutant-run-crosses-row gametile-mutant-run-single gametile-mutant-run-bridges-gap gametile-mutant-mark-kept gametile-mutant-pending-uncounted gametile-mutant-pending-kept gametile-mutant-set-always-marks gametile-mutant-set-unmarked gametile-mutant-set-tile-range gametile-mutant-set-x-bound gametile-mutant-init-clean gametile-mutant-fill-range gametile-mutant-fill-tile-range gametile-mutant-short-write-accepted gametile-mutant-ioctl-failure-ignored gametile-mutant-window-height gametile-mutant-write-source gametile-mutant-load-last-tile gametile-mutant-load-high gametile-mutant-load-count-zero gametile-mutant-load-touch-missing gametile-mutant-touch-range gametile-mutant-load-bytes

.PHONY: gfx-mutants gfx-mutant-exclusive-open gfx-mutant-no-restore \
	gfx-mutant-coord-bound gfx-mutant-rect-order gfx-mutant-circle-right \
	gfx-mutant-circle-left gfx-mutant-window-format \
	gfx-mutant-window-extent gfx-mutant-window-gray \
	gfx-mutant-palette-index gfx-mutant-scroll-range gfx-mutant-scrdef-sum \
	gfx-mutant-scrdef-scroll gfx-mutant-colmod-565 gfx-mutant-colmod-any \
	gfx-mutant-widen-red gfx-mutant-byte-order gfx-mutant-index-mask \
	gfx-mutant-row-padding gfx-mutant-odd-write gfx-mutant-write-overrun \
	gfx-mutant-ioctl-abandon gfx-mutant-close-abandon \
	gfx-mutant-frame-overdraw gfx-mutant-circle-mirror \
	gfx-mutant-circle-swap gfx-mutant-line-error gfx-mutant-line-fast-path \
	gfx-mutant-palette-default gfx-mutant-open-format \
	gfx-mutant-open-scroll gfx-mutant-info-bpp gfx-mutant-gate-cell \
	gfx-mutant-enter-flag gfx-mutant-leave-repaint gfx-mutant-leave-reset \
	gfx-mutant-abort-leave \
	gfx-mutant-tileset-bound-high \
	gfx-mutant-tileset-bound-low \
	gfx-mutant-tileset-count-zero \
	gfx-mutant-tileset-offset \
	gfx-mutant-tileset-overrun \
	gfx-mutant-tileset-abandon \
	gfx-mutant-tilewin-right \
	gfx-mutant-tilewin-bottom \
	gfx-mutant-tilewin-width-zero \
	gfx-mutant-tilewin-height-zero \
	gfx-mutant-tilewin-abandon \
	gfx-mutant-tilewin-restart \
	gfx-mutant-tile-number-bound \
	gfx-mutant-tile-number-continue \
	gfx-mutant-tile-resid-clamp \
	gfx-mutant-tile-excess \
	gfx-mutant-tile-window-right \
	gfx-mutant-tile-row-step \
	gfx-mutant-tile-interleave \
	gfx-mutant-tile-line-stride \
	gfx-mutant-tile-nibble-order \
	gfx-mutant-tile-palette-entry \
	gfx-mutant-tile-open-clear \
	gfx-mutant-info-tiles \
	dump-mutants \
	dump-mutant-early-clear \
	dump-mutant-segment-sum-unchecked \
	dump-mutant-header-sum-unchecked \
	dump-mutant-version-unchecked \
	dump-mutant-magic-unchecked \
	dump-mutant-reserve-dropped \
	dump-mutant-minfree-ignored \
	dump-mutant-bounds-stuck \
	dump-mutant-temp-kept \
	sdspi-mutant-span-window \
	sdspi-mutant-span-padding \
	sdspi-mutant-span-sum-late \
	sdstrat-mutant-dump-busy-unchecked \
	sdstrat-mutant-dump-replaced-ignored \
	sdstrat-mutant-dump-overlap-adjacent \
	sdstrat-mutant-dump-type-ignored \
	sdstrat-mutant-dump-past-card \
	sdstrat-mutant-dump-signature-unchecked \
	sdstrat-mutant-dump-probe-unarmed \
	sdstrat-mutant-dump-device-unchecked \
	sdstrat-mutant-dump-offset-ignored \
	sdstrat-mutant-dump-no-invalidate \
	sdstrat-mutant-dump-sum-dropped \
	sdstrat-mutant-dump-size-unchecked \
	sdstrat-mutant-dump-length-short \
	sdstrat-mutant-dump-table-dropped
