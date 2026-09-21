TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
TARGET_SRC=	${TOPSRC}/usr.bin/touch/touch.c

# The program reaches the host's own headers here, because what this tier
# measures is behavior against a real filesystem and every call it makes --
# utimes, stat, open, getopt, mktime -- the host carries too. The arithmetic
# behind -t and -d is measured against the tree's own mktime by
# tests/libc_contracts check-mktime, which is where that belongs.
HOST_CFLAGS=	-std=gnu17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror

TARGET_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: touch_host
	sh ${.CURDIR}/filesystem_test.sh ${.CURDIR}/touch_host

touch_host: ${TARGET_SRC}
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${TARGET_SRC}

# The same source at the target's width and instruction set, which is the
# compile the shipped program takes.
check-cross: touch_target.o

touch_target.o: ${TARGET_SRC}
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TARGET_SRC}

clean:
	rm -f touch_host touch_target.o

all: check

.PHONY: all check check-cross clean
