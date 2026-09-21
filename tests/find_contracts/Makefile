TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
FINDSRC=	${TOPSRC}/usr.bin/find

# The builders reach the host's own stdio here, because what the tier measures
# is the record boundary the read enforces and the byte stream the encoder
# writes, and fgets, fread and putw behave alike in every hosted stdio. The
# two programs carry prototypes and compile clean under both warning groups,
# so -Werror holds at the full set even though usr.bin/find's own Makefile
# stays at WARNLEVEL=legacy for find.c.
HOST_CFLAGS=	-std=gnu17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror

TARGET_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: bigram_host code_host
	sh ${.CURDIR}/database_test.sh ${.CURDIR}/bigram_host \
	    ${.CURDIR}/code_host ${.CURDIR}/find_codes_reference.py ${FINDSRC}

bigram_host: ${FINDSRC}/bigram.c
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${FINDSRC}/bigram.c

code_host: ${FINDSRC}/code.c
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${FINDSRC}/code.c

# The same sources at the target's width and instruction set, which is the
# compile the shipped programs take.
check-cross: bigram_target.o code_target.o

bigram_target.o: ${FINDSRC}/bigram.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${FINDSRC}/bigram.c

code_target.o: ${FINDSRC}/code.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${FINDSRC}/code.c

clean:
	rm -f bigram_host code_host bigram_target.o code_target.o

all: check

.PHONY: all check check-cross clean
