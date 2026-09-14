HOSTCC?= cc
ARM_COMPILER?= arm-none-eabi-gcc
TARGET_MACHINE?= rp2040
PROG= libc_contracts_test
TARGET_DIR= ../../lib/libc/gen
TOPSRC!= cd ../..; pwd
CFLAGS= -std=c99 -Wall -Wextra -Werror

check: ${PROG} header_contract.o check-aout
	./${PROG}

check-aout:
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} cleantools
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} symlinks tools
	${MAKE} -C ${TOPSRC}/lib/libc_aout/libc MACHINE=${TARGET_MACHINE}
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} \
	    ${TOPSRC}/distrib/obj/boardlibc.${TARGET_MACHINE}/libc.a
	sh ./aout_contracts.sh ${TOPSRC} ${ARM_COMPILER} ${TARGET_MACHINE}

${PROG}: libc_contracts_test.c raise.o ctermid.o
	${HOSTCC} ${CFLAGS} -DL_ctermid=9 -o $@ \
		libc_contracts_test.c raise.o ctermid.o

raise.o: ${TARGET_DIR}/raise.c test_shim.h
	${HOSTCC} ${CFLAGS} -Draise=discobsd_raise -Dkill=test_kill \
		-Dgetpid=test_getpid -include test_shim.h -c -o $@ ${TARGET_DIR}/raise.c

ctermid.o: ${TARGET_DIR}/ctermid.c
	${HOSTCC} ${CFLAGS} -DL_ctermid=9 -Dctermid=discobsd_ctermid \
		-c -o $@ ${TARGET_DIR}/ctermid.c

header_contract.o: header_contract.c
	${HOSTCC} ${CFLAGS} -ffreestanding -c -o $@ header_contract.c

clean:
	rm -f ${PROG} raise.o ctermid.o header_contract.o

all: check

.PHONY: all check check-aout clean
