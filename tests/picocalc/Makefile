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
# sdspi_test runs the SD card protocol layer against a card model that
# replays recorded command transcripts; twenty mutants of sdspi.c must fail
# it.
#
# sdstrat_test compiles the SD disk driver (sd.c) unchanged against the
# kernel's buf.h and disk.h through the headers in shim/ and a card behind
# fake sdspi calls, then drives sdopen, sdstrategy and sdsize with the
# requests physio() and the buffer cache make; nineteen mutants of sd.c must
# fail it.
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
CSTD=		-std=c17 -pedantic-errors -Wall -Wextra -Werror
SAN=		-fsanitize=address,undefined -fno-sanitize-recover=all
# The kernel compiles under the GNU17 profile, and sd.c uses the BSD types
# that profile exposes.
KCSTD=		-std=gnu17 -Wall -Wextra -Werror \
		-DBUS_KHZ=125000 -DCPU_KHZ=125000 \
		-I${TOPSRC}/tests/picocalc/shim -I${TOPSRC}/sys/arch
SRCS=		lcdterm_test.c ${DEV}/lcdterm.c ${DEV}/picokbd_map.c

all: check

check: lcdterm_test kbdreq_test sdspi_test sdstrat_test picoaudio_test \
	font-sync
	./lcdterm_test
	./kbdreq_test
	./sdspi_test
	./sdstrat_test
	./picoaudio_test
	${MAKE} mutants kbdreq-mutants sdspi-mutants sdstrat-mutants \
	    picoaudio-mutants

lcdterm_test: ${SRCS} ${DEV}/lcdterm.h ${DEV}/picokbd_map.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ ${SRCS}

kbdreq_test: kbdreq_test.c ${DEV}/picokbd_req.c ${DEV}/picokbd_req.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ kbdreq_test.c \
	    ${DEV}/picokbd_req.c

sdspi_test: sdspi_test.c ${DEV}/sdspi.c ${DEV}/sdspi.h
	${HOST_CC} ${CSTD} ${SAN} -g -I${DEV} -o $@ sdspi_test.c \
	    ${DEV}/sdspi.c

sdstrat_test: sdstrat_test.c regmodel.h ${DEV}/sd.c ${DEV}/sd.h \
	    ${DEV}/sdspi.h ${DEV}/picocalc_reg.h
	${HOST_CC} ${KCSTD} ${SAN} -g -o $@ sdstrat_test.c ${DEV}/sd.c

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

SDSPI_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${CSTD}" "${DEV}" \
		sdspi_test.c

SDSPI_SRC=	sdspi.c sdspi.h

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
	sdspi-mutant-range-check

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


SDSTRAT_MUT=	@sh ./mutant_unit.sh "${HOST_CC}" "${KCSTD}" "${DEV}" \
		sdstrat_test.c

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
	sdstrat-mutant-raw-segments-unchecked

sdstrat-mutant-whole-block-only:
	${SDSTRAT_MUT} sd.c \
	    's/(bp->b_bcount \& (SD_SECTOR - 1))/(bp->b_bcount \& (DEV_BSIZE - 1))/' sd.c

sdstrat-mutant-stale-resid:
	${SDSTRAT_MUT} sd.c \
	    's/^\tbp->b_resid = 0;/\t(void)0;/' sd.c

sdstrat-mutant-truncation-resid:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid = (n - avail) \* SD_SECTOR;/(void)0;/' sd.c

sdstrat-mutant-truncation-clamp:
	${SDSTRAT_MUT} sd.c \
	    's/if (n > avail) {/if (0) {/' sd.c

sdstrat-mutant-eof-resid:
	${SDSTRAT_MUT} sd.c \
	    's/bp->b_resid = bp->b_bcount;/(void)0;/' sd.c

sdstrat-mutant-eof-past:
	${SDSTRAT_MUT} sd.c \
	    's/if ((u_int)bp->b_blkno == blocks) {/if ((u_int)bp->b_blkno >= blocks) {/' sd.c

sdstrat-mutant-odd-tail:
	${SDSTRAT_MUT} sd.c \
	    's/(p->dp_nsectors + 1) >> 1;/p->dp_nsectors >> 1;/' sd.c

sdstrat-mutant-partition-offset:
	${SDSTRAT_MUT} sd.c \
	    's/sector = p->dp_offset + first;/sector = first;/' sd.c

sdstrat-mutant-block-units:
	${SDSTRAT_MUT} sd.c \
	    's/(u_int)bp->b_blkno \* (DEV_BSIZE \/ SD_SECTOR)/(u_int)bp->b_blkno/' sd.c

sdstrat-mutant-direction:
	${SDSTRAT_MUT} sd.c \
	    's/err = (bp->b_flags \& B_READ) ?/err = (bp->b_flags \& B_READ) == 0 ?/' sd.c

sdstrat-mutant-card-changed:
	${SDSTRAT_MUT} sd.c \
	    's/if (card_changed ||/if (/' sd.c

sdstrat-mutant-open-check:
	${SDSTRAT_MUT} sd.c \
	    's/(sd_openpart \& (1U << part)) == 0) {/0) {/' sd.c

sdstrat-mutant-reinit-early-clear:
	${SDSTRAT_MUT} sd.c \
	    's/^\tbzero(pt, sizeof(pt));/\tbzero(pt, sizeof(pt)); bzero(sdpt, sizeof(sdpt));/' sd.c

sdstrat-mutant-reinit-keep-old:
	${SDSTRAT_MUT} sd.c \
	    's/^\t\tbcopy(pt, sdpt, sizeof(sdpt));/\t\t(void)0;/' sd.c

sdstrat-mutant-raw-read-unchecked:
	${SDSTRAT_MUT} sd.c \
	    '/^sdread(/,/^}/s/sd_misaligned(uio)/0/' sd.c

sdstrat-mutant-raw-write-unchecked:
	${SDSTRAT_MUT} sd.c \
	    '/^sdwrite(/,/^}/s/sd_misaligned(uio)/0/' sd.c

sdstrat-mutant-raw-offset-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_offset \& (DEV_BSIZE - 1)) != 0/0/' sd.c

sdstrat-mutant-raw-sector-granularity:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_offset \& (DEV_BSIZE - 1))/(uio->uio_offset \& (SD_SECTOR - 1))/' sd.c

sdstrat-mutant-raw-segments-unchecked:
	${SDSTRAT_MUT} sd.c \
	    's/(uio->uio_iov\[i\].iov_len \& (DEV_BSIZE - 1)) != 0/0/' sd.c


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
	rm -f lcdterm_test kbdreq_test sdspi_test sdstrat_test \
	    picoaudio_test

.PHONY: all check font-sync mutants mutant-scroll mutant-region \
	mutant-ctrl mutant-reverse-erase mutant-bright mutant-bell \
	mutant-bell-any clean \
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
	sdstrat-mutant-raw-segments-unchecked

.PHONY: picoaudio-mutants \
	picoaudio-mutant-left-override \
	picoaudio-mutant-right-override \
	picoaudio-mutant-override-high \
	picoaudio-mutant-slice-left-on \
	picoaudio-mutant-override-kept \
	picoaudio-mutant-busy-kept
