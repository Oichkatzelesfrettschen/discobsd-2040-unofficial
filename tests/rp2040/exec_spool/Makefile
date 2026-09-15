TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc
KERNEL_BUILD?= ${TOPSRC}/sys/arch/rp2040/compile/PICO
KERNEL_INCLUDES= -I${KERNEL_BUILD} -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
	-I${TOPSRC}/sys/arch/arm/include \
	-I${TOPSRC}/sys/arch/rp2040/dhara/compat \
	-I${TOPSRC}/sys/arch/rp2040/heatshrink \
	-I${TOPSRC}/sys/arch/rp2040/heatshrink/compat
HOST_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -DKERNEL -DSWAPRAM -D__thumb__ \
	-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-sign-compare \
	${KERNEL_INCLUDES}

exec_spool_test: exec_spool_test.c
	${HOST_CC} ${HOST_CFLAGS} -Wl,--gc-sections -o $@ ${.ALLSRC}

check: exec_spool_test
	./exec_spool_test

clean:
	rm -f exec_spool_test

.PHONY: check clean
