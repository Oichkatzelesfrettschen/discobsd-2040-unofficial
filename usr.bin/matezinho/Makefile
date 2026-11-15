TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=matezinho
BINDIR=/usr/bin
OBJS=main.o core.o font.o
CFLAGS+=-DMZ_BOARD

all: ${PROG}

font.o: ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c
	${CC} ${CFLAGS} -c -o $@ ${.ALLSRC}

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@

matezinho.host: main.c core.c matezinho.h
	${HOST_CC} -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Os \
	    -o $@ main.c core.c ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c

# The icon pack and the CC BY-SA 3.0 notices of third_party/mate-icon-theme
# install side by side in /usr/share/matezinho, which the SD root carries
# (the flash profile leaves both out). tools/matezinho/storage.py checks
# every source hash and copies the notices into icons.mzar.artwork; it runs
# under ${PYTHON} with Pillow. The root's file names stop at 63 bytes
# (fsutil), shorter than the upstream tree's, so the notices install as
# README.md and LICENSES/ and the whole companion tree as artwork.tar.
# desktop.sh starts the shell on that pack.
SHAREDIR=/usr/share/matezinho

icons.mzar: ${TOPSRC}/tools/matezinho/storage.py \
	    ${TOPSRC}/third_party/mate-icon-theme/provenance.json
	rm -rf $@ $@.artwork
	${PYTHON} ${TOPSRC}/tools/matezinho/storage.py --profile compact $@ \
	    >/dev/null

install: all icons.mzar
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	mkdir -p ${DESTDIR}${SHAREDIR}/LICENSES
	rm -f ${DESTDIR}${SHAREDIR}/icons.mzar
	cp icons.mzar ${DESTDIR}${SHAREDIR}/icons.mzar
	chmod 444 ${DESTDIR}${SHAREDIR}/icons.mzar
	cp icons.mzar.artwork/README.md ${DESTDIR}${SHAREDIR}/README.md
	cp icons.mzar.artwork/LICENSES/* ${DESTDIR}${SHAREDIR}/LICENSES/
	tar -cf ${DESTDIR}${SHAREDIR}/artwork.tar -C icons.mzar.artwork .
	cp desktop.sh ${DESTDIR}${BINDIR}/desktop
	chmod 755 ${DESTDIR}${BINDIR}/desktop

clean:
	rm -f ${PROG} ${PROG}.elf ${PROG}.host *.o *.dis
	rm -rf icons.mzar icons.mzar.artwork

.PHONY: all install clean
