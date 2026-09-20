TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
HOST_CFLAGS= -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror
TARGET_CFLAGS= -std=c17 -Wall -Wextra -Werror -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: resize_contract_test
	./resize_contract_test

resize_contract_test: resize_contract_test.c
	${HOSTCC} ${HOST_CFLAGS} -o $@ resize_contract_test.c

check-cross: resize_target_contract.o symbol_closure_bad.o
	@if sh ./symbol_closure.sh ${TOPSRC} symbol_closure_bad.o \
	    >/dev/null 2>&1; then \
		echo "resize closure: forbidden-symbol negative control passed" >&2; \
		exit 1; \
	fi
	@echo "resize symbol-closure negative control passed"
	${MAKE} -C ${TOPSRC}/sbin/utilbox MACHINE=rp2040 \
	    resize-symbol-contract

resize_target_contract.o: ${TOPSRC}/usr.bin/resize/resize.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ \
	    ${TOPSRC}/usr.bin/resize/resize.c

symbol_closure_bad.o: symbol_closure_bad.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ symbol_closure_bad.c

clean:
	rm -f resize_contract_test resize_target_contract.o symbol_closure_bad.o

all: check

.PHONY: all check check-cross clean
