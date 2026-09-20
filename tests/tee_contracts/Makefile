TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
HOST_CFLAGS= -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror
TEE_RENAMES= -Dmain=discobsd_tee_main -Dread=test_read \
	-Dwrite=test_write -Dopen=test_open -Dclose=test_close \
	-Dsignal=test_signal -D__sysv_signal=test_signal
TARGET_CFLAGS= -std=c17 -Wall -Wextra -Werror -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: tee_contract_test tee_host
	./tee_contract_test
	sh ./options_test.sh ./tee_host

tee_contract_test: tee_contract_test.c tee_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ tee_contract_test.c tee_source.o

tee_source.o: ${TOPSRC}/usr.bin/tee/tee.c
	${HOSTCC} ${HOST_CFLAGS} ${TEE_RENAMES} -c -o $@ \
	    ${TOPSRC}/usr.bin/tee/tee.c

tee_host: ${TOPSRC}/usr.bin/tee/tee.c
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${TOPSRC}/usr.bin/tee/tee.c

check-cross: tee_target_contract.o

tee_target_contract.o: ${TOPSRC}/usr.bin/tee/tee.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ \
	    ${TOPSRC}/usr.bin/tee/tee.c

clean:
	rm -f tee_contract_test tee_host tee_source.o tee_target_contract.o

all: check

.PHONY: all check check-cross clean
