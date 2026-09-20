TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
HOST_CFLAGS=	-std=c17 -D_POSIX_C_SOURCE=200809L \
		-Wall -Wextra -Werror
RMDIR_RENAMES=	-Dmain=discobsd_rmdir_main -Drmdir=test_rmdir
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: rmdir_contract_test rmdir_host
	./rmdir_contract_test
	sh ./filesystem_test.sh ${.CURDIR}/rmdir_host

rmdir_contract_test: rmdir_contract_test.c rmdir_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ rmdir_contract_test.c rmdir_source.o

rmdir_source.o: ${TOPSRC}/bin/rmdir/rmdir.c
	${HOSTCC} ${HOST_CFLAGS} ${RMDIR_RENAMES} -c -o $@ \
	    ${TOPSRC}/bin/rmdir/rmdir.c

rmdir_host: ${TOPSRC}/bin/rmdir/rmdir.c
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${TOPSRC}/bin/rmdir/rmdir.c

check-cross: rmdir_target_contract.o

rmdir_target_contract.o: ${TOPSRC}/bin/rmdir/rmdir.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TOPSRC}/bin/rmdir/rmdir.c

clean:
	rm -f rmdir_contract_test rmdir_host rmdir_source.o \
	    rmdir_target_contract.o

all: check

.PHONY: all check check-cross clean
