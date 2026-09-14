HOSTCC?= cc
PROG= libc_environment_test
TARGET_DIR= ../../lib/libc/gen
CFLAGS= -std=c99 -Wall -Wextra -Werror
RENAMES= -Dsetenv=discobsd_setenv -Dunsetenv=discobsd_unsetenv \
	-Dputenv=discobsd_putenv -Dgetenv=discobsd_getenv \
	-D_findenv=discobsd_findenv -Denviron=test_environ \
	-include test_shim.h

check: ${PROG}
	./${PROG}

${PROG}: libc_environment_test.c setenv.o getenv.o
	${HOSTCC} ${CFLAGS} -o $@ libc_environment_test.c setenv.o getenv.o

setenv.o: ${TARGET_DIR}/setenv.c test_shim.h
	${HOSTCC} ${CFLAGS} ${RENAMES} -Dmalloc=discobsd_test_malloc \
		-Dfree=discobsd_test_free -c -o $@ ${TARGET_DIR}/setenv.c

getenv.o: ${TARGET_DIR}/getenv.c test_shim.h
	${HOSTCC} ${CFLAGS} ${RENAMES} -c -o $@ ${TARGET_DIR}/getenv.c

clean:
	rm -f ${PROG} setenv.o getenv.o

all: check

.PHONY: all check clean
