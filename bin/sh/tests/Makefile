# Host-side tests for ../edit.c and ../pwname.c: no cross toolchain, no
# device, no pty -- see edit_test.c for why a pipe is enough for the editor.
HOSTCC?=	cc
BIN=		edit_test-host
PWBIN=		pwname_test-host

# _DARWIN_C_SOURCE keeps mkdtemp visible on macOS under the POSIX macro;
# glibc ignores it. edit.c keeps its K&R definitions, which clang 17 and
# later reject under -Werror; gcc ignores the unknown -Wno- flag.
host:		edit_test.c ../edit.c ../edit.h
		${HOSTCC} -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE \
		    -Wall -Wextra -Werror -Wno-deprecated-non-prototype \
		    -DHOSTBUILD -I.. -o ${BIN} edit_test.c ../edit.c

pwname:		pwname_test.c ../pwname.c ../pwname.h
		${HOSTCC} -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE \
		    -Wall -Wextra -Werror -I.. -o ${PWBIN} \
		    pwname_test.c ../pwname.c

test:		host pwname
		./${BIN}
		./${PWBIN}

clean:
		rm -f ${BIN} ${PWBIN} *.o

.PHONY:		host pwname test clean
