TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# subr.c, gettytab.c and init.c, the flag tables, compiled from the tree against the tree's headers,
# so the mode constants are sys/ioctl.h's; the test supplies main and the
# globals main.c would.
HOSTCC?= cc
GETTY_DIR= ${TOPSRC}/libexec/getty
# The tree sources compile under the getty directory's own legacy warning
# profile, -Wall without -Werror; the test itself takes the full profile.
TREE_CFLAGS= -std=gnu17 -Wall -nostdinc -isystem ${TOPSRC}/include -I${GETTY_DIR}
TEST_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -nostdinc -isystem ${TOPSRC}/include \
	-I${GETTY_DIR}
OBJS= getty_subr.o getty_gettytab.o getty_init.o getty_ctype.o

check: setflags_test
	./setflags_test

setflags_test: setflags_test.c ${OBJS}
	${HOSTCC} ${TEST_CFLAGS} -o $@ setflags_test.c ${OBJS}

getty_subr.o: ${GETTY_DIR}/subr.c
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${GETTY_DIR}/subr.c

getty_init.o: ${GETTY_DIR}/init.c
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${GETTY_DIR}/init.c

# gettytab.c classifies bytes through the tree's ctype table.
getty_ctype.o: ${TOPSRC}/lib/libc/gen/ctype_.c
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/gen/ctype_.c

getty_gettytab.o: ${GETTY_DIR}/gettytab.c
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${GETTY_DIR}/gettytab.c

clean:
	rm -f setflags_test ${OBJS}

.PHONY: check clean
