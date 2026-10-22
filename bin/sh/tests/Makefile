# Host-side tests for ../edit.c and ../pwname.c: no cross toolchain, no
# device, no pty -- see edit_test.c for why a pipe is enough for the editor.
HOSTCC?=	cc
BIN=		edit_test-host
PWBIN=		pwname_test-host

# _DARWIN_C_SOURCE keeps mkdtemp visible on macOS under the POSIX macro;
# glibc ignores it.
host:		edit_test.c ../edit.c ../edit.h
		${HOSTCC} -std=c17 -pedantic-errors -D_POSIX_C_SOURCE=200809L \
		    -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
		    -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes \
		    -DHOSTBUILD -I.. -o ${BIN} edit_test.c ../edit.c

pwname:		pwname_test.c ../pwname.c ../pwname.h
		${HOSTCC} -std=c17 -pedantic-errors -D_POSIX_C_SOURCE=200809L \
		    -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
		    -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes \
		    -I.. -o ${PWBIN} \
		    pwname_test.c ../pwname.c

test:		host pwname
		./${BIN}
		./${PWBIN}

clean:
		rm -rf ${BIN} ${PWBIN} *.o out

.PHONY:		host pwname test clean
