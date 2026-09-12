# Host-side smoke test for ../edit.c: no cross toolchain, no device,
# no pty -- see edit_test.c for what it does and why a pipe is enough.
HOSTCC?=	cc
BIN=		edit_test-host

host:		edit_test.c ../edit.c ../edit.h
		${HOSTCC} -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
		    -DHOSTBUILD -I.. -o ${BIN} edit_test.c ../edit.c

test:		host
		./${BIN}

clean:
		rm -f ${BIN} *.o

.PHONY:		host test clean
