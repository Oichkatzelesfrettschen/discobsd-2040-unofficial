#
# Host gate for the PicoCalc PSRAM driver. pio_sim.py executes the PIO
# program in psram_pio.h against a model of the ESP-PSRAM64H that enforces
# the datasheet timings, and psram_xfer.c, compiled for the host, runs
# through it; test_psram.py states each check.
#
# Once the shipped sources pass, each mutant below must fail: a side-set
# value that never raises SCK, data changed in the cycle SCK rises, chip
# select left low, one turnaround clock too few, one address nibble
# dropped, a frame long enough to pass tCEM, a frame that no longer stops at
# a 64-byte block, a word whose comment no longer matches it, and a
# turnaround one pass too long. A gate that passed them would be checking
# nothing.
#
TOPSRC!=	cd ../..; pwd

HOST_CC?=	cc
DEV=		${TOPSRC}/sys/arch/rp2040/dev
ARCH=		${TOPSRC}/sys/arch
CFLAGS=		-std=gnu17 -Wall -Wextra -Werror -DPSRAM_HOST -fPIC -shared
OUT=		${TOPSRC}/.build-logs/psram

all: check

check: libpsram.so
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for check-psram-pio" >&2; \
		exit 2; \
	fi
	mkdir -p ${OUT}
	"${PYTHON}" test_psram.py --lib ./libpsram.so \
	    --header ${DEV}/psram_pio.h --xfer-header ${DEV}/psram_xfer.h \
	    --json ${OUT}/psram-frames.json
	${MAKE} mutants

libpsram.so: psram_host.c psram_host.h ${DEV}/psram_xfer.c \
    ${DEV}/psram_xfer.h ${DEV}/psram_pio.h
	${HOST_CC} ${CFLAGS} -I. -I${ARCH} -o $@ psram_host.c \
	    ${DEV}/psram_xfer.c

MUT=		@sh ./mutant.sh "${HOST_CC}" "${PYTHON}" "${DEV}"

mutants: mutant-sideset mutant-setup mutant-cs mutant-wait mutant-address \
	mutant-tcem mutant-block mutant-comment mutant-turnaround

# Word 7's side-set value 2 raises SCK; 0 leaves it low (word and comment
# agree, so only the waveform checks can catch it).
mutant-sideset:
	${MUT} psram_pio.h \
	    's/0x1046,\(.*\)jmp x--, 6              side 2/0x0046,\1jmp x--, 6              side 0/'

# Word 6 changes the data in the cycle SCK rises: setup violated.
mutant-setup:
	${MUT} psram_pio.h \
	    's/0x6004,\(.*\)out pins, 4             side 0/0x7804,\1out pins, 4             side 3/'

# Words 0 and 18 leave CS low between frames.
mutant-cs:
	${MUT} psram_pio.h \
	    's/0x0800\(.*\)jmp 0                   side 1/0x0000\1jmp 0                   side 0/; s/0x88a0,\(.*\)pull block              side 1/0x80a0,\1pull block              side 0/'

# One turnaround clock too few.
mutant-wait:
	${MUT} psram_xfer.c 's/\(PSRAM_READ_WAIT[[:space:]][[:space:]]*\)6/\15/'

# One address nibble dropped from the read command.
mutant-address:
	${MUT} psram_xfer.c 's/PSRAM_CTRL_WORD(8, PSRAM_READ_WAIT/PSRAM_CTRL_WORD(7, PSRAM_READ_WAIT/'

# A frame of 128 bytes holds CS low past tCEM.
mutant-tcem:
	${MUT} psram_xfer.h 's/\(PSRAM_CHUNK[[:space:]][[:space:]]*\)64/\1128/'

# A frame that ignores the 64-byte block boundary.
mutant-block:
	${MUT} psram_xfer.c 's/PSRAM_CHUNK - (addr \& (PSRAM_CHUNK - 1))/PSRAM_CHUNK/'

# A word changed without its comment: the decode check catches it.
mutant-comment:
	${MUT} psram_pio.h 's/0x4004,/0x4005,/'

# The turnaround one pass longer than the input path needs: the read lands
# on the next nibble, the sampling the model showed before the output and
# input register stages were timed on clk_sys.
mutant-turnaround:
	${MUT} psram_pio.h 's/(wait) + 1) << 17/(wait) + 2) << 17/'

clean:
	rm -f libpsram.so

.PHONY: all check mutants mutant-sideset mutant-setup mutant-cs mutant-wait \
	mutant-address mutant-tcem mutant-block mutant-comment \
	mutant-turnaround clean
