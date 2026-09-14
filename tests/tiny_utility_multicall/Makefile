TOPSRC!=cd ../..; pwd

HOST_CC?=	cc
HOST_CFLAGS?=	-O2 -Wall -Wextra -Werror -Wformat=2

all: driver probe

driver: driver.c ${TOPSRC}/sbin/adminbox/tiny_applets.c
	${HOST_CC} ${HOST_CFLAGS} -o $@ driver.c \
	    ${TOPSRC}/sbin/adminbox/tiny_applets.c

probe: probe.c
	${HOST_CC} ${HOST_CFLAGS} -o $@ probe.c

check: all
	sh check.sh ${.CURDIR}/driver ${.CURDIR}/probe

clean:
	rm -f driver probe

.PHONY: all check clean
