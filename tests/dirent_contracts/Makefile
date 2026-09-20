TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
TARGET_DIR=	${TOPSRC}/lib/libc/gen
RENAMES=	-Dopendir=discobsd_opendir -Dreaddir=discobsd_readdir \
		-Dclosedir=discobsd_closedir -Dseekdir=discobsd_seekdir \
		-Dtelldir=discobsd_telldir -Dopen=test_open \
		-Dclose=test_close -Dread=test_read -Dlseek=test_lseek \
		-Dmalloc=test_malloc -Dfree=test_free \
		-Derrno=dirent_test_errno
HOST_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
		-fno-builtin -nostdinc \
		-isystem ${TOPSRC}/include ${RENAMES}
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
		-fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-nostdinc -I${TOPSRC}/include -Os -ffreestanding \
		-fno-builtin
HOST_OBJS=	dirent_opendir.o dirent_readdir.o dirent_closedir.o \
		dirent_seekdir.o dirent_telldir.o
TARGET_OBJS=	dirent_opendir_target.o dirent_readdir_target.o \
		dirent_closedir_target.o dirent_seekdir_target.o \
		dirent_telldir_target.o dirent_layout_target.o

check: dirent_contract_test
	./dirent_contract_test

dirent_contract_test: dirent_contract_test.c ${HOST_OBJS}
	${HOSTCC} ${HOST_CFLAGS} -o $@ dirent_contract_test.c ${HOST_OBJS}

dirent_opendir.o: ${TARGET_DIR}/opendir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/opendir.c

dirent_readdir.o: ${TARGET_DIR}/readdir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/readdir.c

dirent_closedir.o: ${TARGET_DIR}/closedir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/closedir.c

dirent_seekdir.o: ${TARGET_DIR}/seekdir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/seekdir.c

dirent_telldir.o: ${TARGET_DIR}/telldir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/telldir.c

check-cross: ${TARGET_OBJS}
	@if ${GCCPREFIX}-nm -u dirent_telldir_target.o | \
	    grep '[[:space:]]lseek$$' >/dev/null; then \
		echo "dirent: telldir target object calls lseek" >&2; \
		exit 1; \
	fi

dirent_layout_target.o: dirent_layout.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ dirent_layout.c

.for source_name in opendir readdir closedir seekdir telldir
dirent_${source_name}_target.o: ${TARGET_DIR}/${source_name}.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/${source_name}.c
.endfor

clean:
	rm -f dirent_contract_test ${HOST_OBJS} ${TARGET_OBJS}

all: check

.PHONY: all check check-cross clean
