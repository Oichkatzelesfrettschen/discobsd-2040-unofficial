#
# 1997/6/25 - Steven Schultz
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

SRCS		= id.c
OBJS		= id.o
MAN		= id.0 groups.0 whoami.0 logname.0
ALIASES		= groups whoami logname

all:            id ${ALIASES} ${MAN}

id:             ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o id.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S id.elf > id.dis
		${SIZE} id.elf
		${ELF2AOUT} id.elf $@ && rm id.elf

.for alias in ${ALIASES}
${alias}:       id
		rm -f $@
		ln id $@
.endfor

.SUFFIXES:      .0 .1

.1.0:
		${MANROFF} $*.1 > $@

clean:
		rm -f *.o *.elf ${MAN} id groups whoami logname *.elf *.dis tags *~

depend:         ${SRCS}
		mkdep ${CFLAGS} ${SRCS}

install:        all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} id ${DESTDIR}/usr/bin/id
		rm -f ${DESTDIR}/usr/bin/groups ${DESTDIR}/usr/bin/whoami \
		    ${DESTDIR}/usr/bin/logname
		ln ${DESTDIR}/usr/bin/id ${DESTDIR}/usr/bin/groups
		ln ${DESTDIR}/usr/bin/id ${DESTDIR}/usr/bin/whoami
		ln ${DESTDIR}/usr/bin/id ${DESTDIR}/usr/bin/logname

lint:           ${SRCS}
		lint -hax ${SRCS}

tags:           ${SRCS}
		ctags ${SRCS}

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
