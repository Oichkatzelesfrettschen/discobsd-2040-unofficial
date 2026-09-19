TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
HOST_CFLAGS=	-std=c17 -D_POSIX_C_SOURCE=200809L \
		-Wall -Wextra -Werror
CAT_RENAMES=	-Dmain=discobsd_cat_main -Dfastcat=discobsd_fastcat \
		-Dread=test_read -Dwrite=test_write -Dfileno=test_fileno \
		-Dperror=test_perror -DCAT_TEST_BUFSIZ=1024
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os

check: cat_contract_test cat_host
	./cat_contract_test
	sh ./options_test.sh ./cat_host

cat_contract_test: cat_contract_test.c cat_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ cat_contract_test.c cat_source.o

cat_source.o: ${TOPSRC}/bin/cat/cat.c
	${HOSTCC} ${HOST_CFLAGS} ${CAT_RENAMES} -c -o $@ \
	    ${TOPSRC}/bin/cat/cat.c

cat_host: ${TOPSRC}/bin/cat/cat.c
	${HOSTCC} ${HOST_CFLAGS} -DCAT_TEST_BUFSIZ=1024 -o $@ \
	    ${TOPSRC}/bin/cat/cat.c

check-cross: cat_target_contract.o

# Compiling the exact target source exercises its BUFSIZ, MAXBSIZE and
# DEV_BSIZE assertions as well as the full warning policy.
cat_target_contract.o: ${TOPSRC}/bin/cat/cat.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TOPSRC}/bin/cat/cat.c

clean:
	rm -f cat_contract_test cat_source.o cat_target_contract.o cat_host

all: check

.PHONY: all check check-cross clean
