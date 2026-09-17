# Host-side smoke test for ../edit.c: no cross toolchain, no device,
# no pty -- see edit_test.c for what it does and why a pipe is enough.
HOSTCC?=	cc
BIN=		edit_test-host

# _DARWIN_C_SOURCE keeps mkdtemp visible on macOS under the POSIX macro;
# glibc ignores it. edit.c keeps its K&R definitions, which clang 17 and
# later reject under -Werror; gcc ignores the unknown -Wno- flag.
host:		edit_test.c ../edit.c ../edit.h
		${HOSTCC} -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE \
		    -Wall -Wextra -Werror -Wno-deprecated-non-prototype \
		    -DHOSTBUILD -I.. -o ${BIN} edit_test.c ../edit.c

test:		host
		./${BIN}

clean:
		rm -f ${BIN} *.o

.PHONY:		host test clean
