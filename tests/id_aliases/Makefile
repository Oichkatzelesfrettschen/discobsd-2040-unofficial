TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
PROG= id_host
TARGET= ../../usr.bin/id/id.c
CFLAGS= -std=gnu17 -Wall -Wextra -Werror
# The BSD functions come from libbsd-overlay on Linux and from libc on the
# BSDs and macOS; sys.mk resolves both to the right flags.
HOST_CFLAGS= ${_LIBBSD_CFLAGS}
HOST_LIBS= ${_LIBBSD_LIBS}
# The getgrouplist rename lands on the host's own declaration as well, and
# macOS declares its group array as int * where id.c passes gid_t; the
# widths agree and identity_stubs.c is the callee, so that check is off
# for id.o alone.
ID_CFLAGS= -Wno-pointer-sign
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
	${HOSTCC} ${CFLAGS} ${HOST_CFLAGS} ${ID_CFLAGS} ${RENAMES} -c -o $@ ${TARGET}

${ALIASES}: ${PROG}
	ln -sf ${PROG} $@

clean:
	rm -f ${PROG} id.o ${ALIASES}

all: check

.PHONY: all check clean
