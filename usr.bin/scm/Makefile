TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
# -Wall -Wextra raises 54 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Os -Wall

OBJS            = scm.o func.o
TESTS           = tests/*.scm

all:            scm ${MAN}

scm:            ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o scm.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S scm.elf > scm.dis
		${SIZE} scm.elf
		${ELF2AOUT} scm.elf $@ && rm scm.elf

clean:
		rm -f *.o scm *.elf *.dis *~ tests/*.out

install:        all
		${INSTALL} scm ${DESTDIR}/usr/bin/

tests:          scm $(TESTS) ALWAYS
		for i in $(TESTS); do \
                        base=`basename $$i .scm`;\
                        ./scm -v $$i > tests/$$base.out;\
                        diff tests/$$base.expected tests/$$base.out; done

ALWAYS:

expected:       scm $(TESTS)
		for i in $(TESTS); do \
                        base=`basename $$i .scm`;\
                        ./scm -v $$i > tests/$$base.expected; done
