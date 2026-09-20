TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
SOURCE=		${TOPSRC}/usr.bin/unifdef/unifdef.c
HOST_CFLAGS=	-std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
SANITIZER_FLAGS= -fsanitize=address,undefined -fno-omit-frame-pointer
SOURCE_RENAMES=	-Dmain=unifdef_program_main
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-Wmissing-prototypes -Wold-style-definition -fno-common \
		-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
		-mfloat-abi=soft -ffreestanding -nostdinc -I${TOPSRC}/include -Os

check: unifdef_contract_test unifdef_contract_sanitize unifdef_host \
		unifdef_bad_elif_test \
		unifdef_bad_flush_test
	./unifdef_contract_test
	./unifdef_contract_sanitize
	sh ./unifdef_cli_test.sh ./unifdef_host
	@if ./unifdef_bad_elif_test >/dev/null 2>&1; then \
		echo "unifdef permissive-elif negative control passed" >&2; exit 1; \
	fi
	@echo "unifdef permissive-elif negative control rejected"
	@if ./unifdef_bad_flush_test >/dev/null 2>&1; then \
		echo "unifdef ignored-flush negative control passed" >&2; exit 1; \
	fi
	@echo "unifdef ignored-flush negative control rejected"

unifdef_contract_test: unifdef_contract_test.c unifdef_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ unifdef_contract_test.c unifdef_source.o

unifdef_host: ${SOURCE}
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${SOURCE}

unifdef_source.o: ${SOURCE}
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ ${SOURCE}

unifdef_contract_sanitize: unifdef_contract_test.c unifdef_sanitize.o
	${HOSTCC} ${HOST_CFLAGS} ${SANITIZER_FLAGS} -o $@ \
	    unifdef_contract_test.c unifdef_sanitize.o

unifdef_sanitize.o: ${SOURCE}
	${HOSTCC} ${HOST_CFLAGS} ${SANITIZER_FLAGS} ${SOURCE_RENAMES} \
	    -c -o $@ ${SOURCE}

unifdef_bad_elif.c: ${SOURCE}
	sed 's/{ "elif", DIRECTIVE_ELIF }/{ "elif", DIRECTIVE_PLAIN }/' \
	    ${SOURCE} > $@

unifdef_bad_elif.o: unifdef_bad_elif.c
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ unifdef_bad_elif.c

unifdef_bad_elif_test: unifdef_contract_test.c unifdef_bad_elif.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ unifdef_contract_test.c unifdef_bad_elif.o

unifdef_bad_flush.c: ${SOURCE}
	sed 's/if (fflush(output) == EOF)/if (0)/' ${SOURCE} > $@

unifdef_bad_flush.o: unifdef_bad_flush.c
	${HOSTCC} ${HOST_CFLAGS} ${SOURCE_RENAMES} -c -o $@ unifdef_bad_flush.c

unifdef_bad_flush_test: unifdef_contract_test.c unifdef_bad_flush.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ unifdef_contract_test.c unifdef_bad_flush.o

check-cross: unifdef_target.o
	@test -z "$$(${GCCPREFIX}-nm -u unifdef_target.o | \
	    grep -E '__aeabi_(uidiv|uldiv|idiv)' || true)"
	@${GCCPREFIX}-size unifdef_target.o
	@echo "unifdef Cortex-M0+ object avoids compiler division helpers"

unifdef_target.o: ${SOURCE}
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${SOURCE}

clean:
	rm -f unifdef_contract_test unifdef_contract_sanitize unifdef_host \
		unifdef_source.o unifdef_sanitize.o \
		unifdef_bad_elif.c unifdef_bad_elif.o unifdef_bad_elif_test \
		unifdef_bad_flush.c unifdef_bad_flush.o unifdef_bad_flush_test \
		unifdef_target.o

all: check

.PHONY: all check check-cross clean
