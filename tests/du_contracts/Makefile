TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
HOST_NM?= nm
ARM_COMPILER?= ${GCCPREFIX}-gcc
HOST_CFLAGS= -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
	-Iinclude -DDU_PATH_SIZE=1024
DU_RENAMES= -Dmalloc=du_test_malloc -Dfree=du_test_free
TARGET_CFLAGS= -std=c17 -Wall -Wextra -Werror -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: du_host directory_cookie_bad.o
	@if sh ./directory_resume_closure.sh ${HOST_NM} \
	    directory_cookie_bad.o >/dev/null 2>&1; then \
		echo "du directory resume negative control passed" >&2; \
		exit 1; \
	fi
	@echo "du directory resume negative control passed"
	sh ./directory_resume_closure.sh ${HOST_NM} du_source.o
	sh ./filesystem_test.sh ./du_host

du_host: du_source.o malloc_control.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ du_source.o malloc_control.o

du_source.o: ${TOPSRC}/usr.bin/du/du.c
	${HOSTCC} ${HOST_CFLAGS} ${DU_RENAMES} -c -o $@ \
	    ${TOPSRC}/usr.bin/du/du.c

malloc_control.o: malloc_control.c
	${HOSTCC} -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
	    -c -o $@ malloc_control.c

directory_cookie_bad.o: directory_cookie_bad.c
	${HOSTCC} -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
	    -c -o $@ directory_cookie_bad.c

check-cross: du_target_contract.o

du_target_contract.o: ${TOPSRC}/usr.bin/du/du.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ \
	    ${TOPSRC}/usr.bin/du/du.c

clean:
	rm -f du_host du_source.o du_target_contract.o malloc_control.o \
	    directory_cookie_bad.o

all: check

.PHONY: all check check-cross clean
