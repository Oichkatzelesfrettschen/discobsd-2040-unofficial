TOPSRC!=cd ../..; pwd

include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
PYTHON?=
include ${TOPSRC}/tools/test-execution.mk
STRICT_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes
SET_PGID_CFLAGS=	${STRICT_CFLAGS} -nostdinc -isystem ${TOPSRC}/include \
		-Dsetpgid=db_setpgid -Dsetpgrp=test_setpgrp -Dgetpid=test_getpid
TARGET_CFLAGS=	${STRICT_CFLAGS} -fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-ffreestanding -nostdinc -isystem ${TOPSRC}/include -Os

check: check-behavior check-setpgid

check-behavior:
	@: "$${PYTHON:?set PYTHON to the intended interpreter}"
	${TEST_EXECUTION} run utility.timeout.native -- \
	    sh ${.CURDIR}/run_behavior_test.sh ${TOPSRC} ${HOSTCC:Q}

check-setpgid: setpgid_contract_test
	./setpgid_contract_test

setpgid_contract_test: setpgid_contract_test.c setpgid_contract.o
	${HOSTCC} ${STRICT_CFLAGS} -o $@ setpgid_contract_test.c \
	    setpgid_contract.o

setpgid_contract.o: ${TOPSRC}/lib/libc/gen/setpgid.c
	${HOSTCC} ${SET_PGID_CFLAGS} -c -o $@ \
	    ${TOPSRC}/lib/libc/gen/setpgid.c

check-cross: check-stack-diagnostic check-stack
	${ARM_COMPILER} ${TARGET_CFLAGS} -fsyntax-only \
	    ${TOPSRC}/usr.bin/timeout/timeout.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -fsyntax-only \
	    ${TOPSRC}/lib/libc/gen/setpgid.c

check-stack:
	@: "$${PYTHON:?set PYTHON to the intended interpreter}"
	${PYTHON} ${.CURDIR}/stack_test.py ${TOPSRC} ${ARM_COMPILER:Q}

check-stack-diagnostic:
	@: "$${PYTHON:?set PYTHON to the intended interpreter}"
	cd ${.CURDIR} && ${PYTHON} -m unittest stack_test_test.py

check-target-link:
	${MAKE} -C ${TOPSRC}/usr.bin/timeout MACHINE=${MACHINE} clean all

clean:
	rm -f setpgid_contract_test setpgid_contract.o

.PHONY:	check check-behavior check-setpgid check-cross check-stack \
	check-stack-diagnostic \
	check-target-link clean
