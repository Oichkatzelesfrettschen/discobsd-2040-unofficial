TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
SOURCE=		${TOPSRC}/usr.bin/colrm/colrm.c
HOST_CFLAGS=	-std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
SOURCE_RENAMES=	-Dmain=colrm_program_main
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-Wmissing-prototypes -Wold-style-definition -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -ffreestanding -nostdinc -I${TOPSRC}/include -Os

check: colrm_contract_test colrm_bounds_bad_test colrm_flush_bad_test
	./colrm_contract_test
	@if ./colrm_bounds_bad_test >/dev/null 2>&1; then \
		echo "colrm exclusive-stop negative control passed" >&2; exit 1; \
	fi
	@echo "colrm exclusive-stop negative control rejected"
	@if ./colrm_flush_bad_test >/dev/null 2>&1; then \
		echo "colrm ignored-flush negative control passed" >&2; exit 1; \
	fi
	@echo "colrm ignored-flush negative control rejected"

colrm_contract_test: colrm_contract_test.c colrm_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ colrm_contract_test.c colrm_source.o

colrm_bounds_bad_test: colrm_contract_test.c colrm_bounds_bad.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ colrm_contract_test.c colrm_bounds_bad.o

colrm_flush_bad_test: colrm_contract_test.c colrm_flush_bad.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ colrm_contract_test.c colrm_flush_bad.o

colrm_source.o: ${SOURCE}
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ ${SOURCE}

colrm_bounds_bad.c: ${SOURCE}
	sed 's/column > stop/column >= stop/' ${SOURCE} > $@

colrm_bounds_bad.o: colrm_bounds_bad.c
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ colrm_bounds_bad.c

colrm_flush_bad.c: ${SOURCE}
	sed 's/if (fflush(output) == EOF)/if (0)/' ${SOURCE} > $@

colrm_flush_bad.o: colrm_flush_bad.c
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ colrm_flush_bad.c

check-cross: colrm_target.o
	@test -z "$$(${GCCPREFIX}-nm -u colrm_target.o | \
	    grep -E '__aeabi_(uidiv|uldiv|idiv)' || true)"
	@echo "colrm Cortex-M0+ object avoids compiler division helpers"

colrm_target.o: ${SOURCE}
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${SOURCE}

clean:
	rm -f colrm_contract_test colrm_bounds_bad_test colrm_flush_bad_test \
		colrm_source.o colrm_bounds_bad.c colrm_bounds_bad.o \
		colrm_flush_bad.c colrm_flush_bad.o colrm_target.o

all: check

.PHONY: all check check-cross clean
