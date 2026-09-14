HOSTCC?= cc
PROG= id_host
TARGET= ../../usr.bin/id/id.c
CFLAGS= -std=gnu17 -Wall -Wextra -Werror
HOST_CFLAGS!= pkg-config libbsd-overlay --cflags
HOST_LIBS!= pkg-config libbsd-overlay --libs
RENAMES= -Dmain=discobsd_id_main -Dgetuid=test_getuid \
	-Dgeteuid=test_geteuid -Dgetgid=test_getgid -Dgetegid=test_getegid \
	-Dgetlogin=test_getlogin -Dgetgroups=test_getgroups \
	-Dgetgrouplist=test_getgrouplist -Dgetpwuid=test_getpwuid \
	-Dgetpwnam=test_getpwnam -Dgetgrgid=test_getgrgid
ALIASES= whoami groups logname

check: ${PROG} ${ALIASES}
	sh ./check.sh

${PROG}: driver.c identity_stubs.c id.o
	${HOSTCC} ${CFLAGS} ${HOST_CFLAGS} -o $@ driver.c identity_stubs.c \
		id.o ${HOST_LIBS}

id.o: ${TARGET}
	${HOSTCC} ${CFLAGS} ${HOST_CFLAGS} ${RENAMES} -c -o $@ ${TARGET}

${ALIASES}: ${PROG}
	ln -sf ${PROG} $@

clean:
	rm -f ${PROG} id.o ${ALIASES}

all: check

.PHONY: all check clean
