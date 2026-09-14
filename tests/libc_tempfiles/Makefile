HOSTCC?= cc
PROG= libc_tempfiles_test
TARGET= ../../lib/libc/compat/tmpnam.c
CFLAGS= -std=c99 -Wall -Wextra -Werror
RENAMES= -Dtmpfile=discobsd_tmpfile -Dtmpnam=discobsd_tmpnam \
	-Dtempnam=discobsd_tempnam -Dmkstemp=test_mkstemp \
	-Dunlink=test_unlink -Dfdopen=test_fdopen -Dclose=test_close \
	-Dgetenv=test_getenv -Dmktemp=test_mktemp -Dmalloc=test_malloc \
	-Dfree=test_free -include test_shim.h

check: ${PROG}
	./${PROG}

${PROG}: libc_tempfiles_test.c tmpnam.o
	${HOSTCC} ${CFLAGS} -o $@ libc_tempfiles_test.c tmpnam.o

tmpnam.o: ${TARGET} test_shim.h
	${HOSTCC} ${CFLAGS} ${RENAMES} -c -o $@ ${TARGET}

clean:
	rm -f ${PROG} tmpnam.o

all: check

.PHONY: all check clean
