TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The real getty mode path, tables and parsers compile against the tree's
# headers, so the mode constants come from sys/ioctl.h. The test supplies
# ioctl behavior and records each transition.
HOSTCC?= cc
GETTY_DIR= ${TOPSRC}/libexec/getty
LOGIN_DIR= ${TOPSRC}/usr.bin/login
GETTY_HEADERS= ${GETTY_DIR}/gettytab.h ${GETTY_DIR}/extern.h
# The exact tree sources and the test use a fatal host warning profile.
TREE_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	-nostdinc -isystem ${TOPSRC}/include -I${GETTY_DIR} -Dioctl=test_ioctl
GETTYTAB_TEST_CFLAGS= -Dopen=test_open -Dread=test_read -Dclose=test_close
TEST_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -nostdinc -isystem ${TOPSRC}/include \
	-I${GETTY_DIR}
GC_SECTIONS!= if [ "$$(uname -s)" = Darwin ] ; then echo -Wl,-dead_strip ; \
	else echo -Wl,--gc-sections ; fi
OBJS= getty_main.o getty_subr.o getty_gettytab_test.o getty_init.o getty_ctype.o

check: setflags_test setflags_bad_test setflags_parser_bad_test login_modes_test
	./setflags_test
	@if ./setflags_bad_test >/dev/null 2>&1; then \
		echo "getty contract: local-mode negative control passed" >&2; \
		exit 1; \
	fi
	@echo "getty local-mode negative control passed"
	@if ./setflags_parser_bad_test >/dev/null 2>&1; then \
		echo "getty contract: parser negative control passed" >&2; \
		exit 1; \
	fi
	@echo "getty parser negative control passed"
	./login_modes_test
	sh ./check_login_handoff.sh ${LOGIN_DIR}/login.c

setflags_test: setflags_test.c ${GETTY_HEADERS} ${OBJS}
	${HOSTCC} ${TEST_CFLAGS} ${GC_SECTIONS} -o $@ setflags_test.c ${OBJS}

setflags_bad_test: setflags_test.c ${GETTY_HEADERS} getty_modes_bad.o \
	getty_subr.o getty_gettytab_test.o getty_init.o getty_ctype.o
	${HOSTCC} ${TEST_CFLAGS} ${GC_SECTIONS} -o $@ setflags_test.c \
	    getty_modes_bad.o getty_subr.o getty_gettytab_test.o getty_init.o \
	    getty_ctype.o

setflags_parser_bad_test: setflags_test.c ${GETTY_HEADERS} getty_parser_bad.o \
	getty_main.o getty_subr.o getty_gettytab_parser_bad.o getty_init.o \
	getty_ctype.o
	${HOSTCC} ${TEST_CFLAGS} ${GC_SECTIONS} -o $@ setflags_test.c \
	    getty_parser_bad.o getty_main.o getty_subr.o \
	    getty_gettytab_parser_bad.o getty_init.o getty_ctype.o

getty_main.o: ${GETTY_DIR}/main.c ${GETTY_HEADERS}
	${HOSTCC} ${TREE_CFLAGS} -Dmain=getty_program_main -c -o $@ ${GETTY_DIR}/main.c

getty_modes_bad.o: getty_modes_bad.c ${GETTY_HEADERS}
	${HOSTCC} ${TEST_CFLAGS} -c -o $@ getty_modes_bad.c

getty_subr.o: ${GETTY_DIR}/subr.c ${GETTY_HEADERS}
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${GETTY_DIR}/subr.c

getty_init.o: ${GETTY_DIR}/init.c ${GETTY_DIR}/gettytab.h
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${GETTY_DIR}/init.c

# gettytab.c classifies bytes through the tree's ctype table.
getty_ctype.o: ${TOPSRC}/lib/libc/gen/ctype_.c
	${HOSTCC} ${TREE_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/gen/ctype_.c

getty_gettytab_test.o: ${GETTY_DIR}/gettytab.c ${GETTY_HEADERS}
	${HOSTCC} ${TREE_CFLAGS} ${GETTYTAB_TEST_CFLAGS} -c -o $@ \
	    ${GETTY_DIR}/gettytab.c

getty_gettytab_parser_bad.o: ${GETTY_DIR}/gettytab.c ${GETTY_HEADERS}
	${HOSTCC} ${TREE_CFLAGS} ${GETTYTAB_TEST_CFLAGS} \
	    -Dgetflag=real_getflag -c -o $@ ${GETTY_DIR}/gettytab.c

getty_parser_bad.o: getty_parser_bad.c ${GETTY_DIR}/gettytab.h
	${HOSTCC} ${TEST_CFLAGS} -c -o $@ getty_parser_bad.c

login_modes_test: login_modes_test.c login_tty_modes.o
	${HOSTCC} ${TEST_CFLAGS} -I${LOGIN_DIR} -o $@ login_modes_test.c login_tty_modes.o

login_tty_modes.o: ${LOGIN_DIR}/tty_modes.c ${LOGIN_DIR}/tty_modes.h
	${HOSTCC} ${TEST_CFLAGS} -I${LOGIN_DIR} -Dioctl=test_ioctl -c -o $@ ${LOGIN_DIR}/tty_modes.c

clean:
	rm -f setflags_test setflags_bad_test setflags_parser_bad_test \
	    getty_modes_bad.o getty_parser_bad.o getty_gettytab.o \
	    getty_gettytab_parser_bad.o \
	    login_modes_test login_tty_modes.o ${OBJS}

.PHONY: check clean
