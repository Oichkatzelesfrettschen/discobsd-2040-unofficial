HOSTCC?= cc
PROGS= libc_tempfiles_test native_tempfiles_test
TARGET= ../../lib/libc/compat/tmpnam.c
CFLAGS= -std=c99 -Wall -Wextra -Werror
NATIVE_CFLAGS= -D_DEFAULT_SOURCE -include native_shim.h
# tmpnam.c calls mktemp, which macOS's <unistd.h> declares deprecated;
# the attribute lands on the renamed symbol too, and the test supplies
# its own mktemp, so that one warning is off for the two tmpnam objects.
MKTEMP_CFLAGS= -Wno-deprecated-declarations
RENAMES= -Dtmpfile=discobsd_tmpfile -Dtmpnam=discobsd_tmpnam \
	-Dtempnam=discobsd_tempnam -Dmkstemp=test_mkstemp \
	-Dunlink=test_unlink -Dfdopen=test_fdopen -Dclose=test_close \
	-Dgetenv=test_getenv -Dmktemp=test_mktemp -Dmalloc=test_malloc \
	-Dfree=test_free -include test_shim.h

check: ${PROGS} header_contract.o
	./libc_tempfiles_test
	./native_tempfiles_test

libc_tempfiles_test: libc_tempfiles_test.c tmpnam.o
	${HOSTCC} ${CFLAGS} -o $@ libc_tempfiles_test.c tmpnam.o

tmpnam.o: ${TARGET} test_shim.h
	${HOSTCC} ${CFLAGS} ${MKTEMP_CFLAGS} ${RENAMES} -c -o $@ ${TARGET}

native_tempfiles_test: native_tempfiles_test.c native_tmpnam.o \
		native_mktemp.o native_shim.h
	${HOSTCC} ${CFLAGS} ${NATIVE_CFLAGS} -o $@ \
		native_tempfiles_test.c native_tmpnam.o native_mktemp.o

native_tmpnam.o: ${TARGET} native_shim.h
	${HOSTCC} ${CFLAGS} ${MKTEMP_CFLAGS} -Dtmpfile=discobsd_tmpfile \
		-Dtmpnam=discobsd_tmpnam -Dtempnam=discobsd_tempnam \
		${NATIVE_CFLAGS} -c -o $@ ${TARGET}

native_mktemp.o: ../../lib/libc/gen/mktemp.c native_shim.h
	${HOSTCC} ${CFLAGS} ${NATIVE_CFLAGS} -c -o $@ \
		../../lib/libc/gen/mktemp.c

header_contract.o: header_contract.c
	${HOSTCC} ${CFLAGS} -ffreestanding -c -o $@ header_contract.c

clean:
	rm -f ${PROGS} tmpnam.o native_tmpnam.o native_mktemp.o \
		header_contract.o

all: check

.PHONY: all check clean
