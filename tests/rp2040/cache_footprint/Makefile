TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?=	cc
KERNEL_BUILD?=	${TOPSRC}/sys/arch/rp2040/compile/PICO
KERNEL_INCLUDES=	-I${KERNEL_BUILD} -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
		-I${TOPSRC}/sys/arch/arm/include \
		-I${TOPSRC}/sys/arch/rp2040/dhara/compat \
		-I${TOPSRC}/sys/arch/rp2040/heatshrink \
		-I${TOPSRC}/sys/arch/rp2040/heatshrink/compat
HOST_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
		-fno-common -ffunction-sections -fdata-sections -DKERNEL \
		${KERNEL_INCLUDES}
LINEAR_CFLAGS=	-DLINEAR_NAME_CACHE -DLINEAR_BUFFER_CACHE \
		-DLINEAR_INODE_CACHE -DCOMPACT_INODE_FIELDS \
		-DSINGLE_UFS_ROOT -DNMOUNT=1 -DNAMECACHE_TEST

# The host's 64-bit off_t changes two unrelated comparisons in ufs_namei.c.
# The target compiler still checks those lines with -Wall -Werror.
ufs_namei.test.o: ${TOPSRC}/sys/kern/ufs_namei.c
	${HOST_CC} ${HOST_CFLAGS} -Wno-sign-compare -c -o $@ ${.ALLSRC}

cache_footprint.o: cache_footprint.c
	${HOST_CC} ${HOST_CFLAGS} -c -o $@ ${.ALLSRC}

cache_footprint: cache_footprint.o ufs_namei.test.o
	${HOST_CC} -Wl,--gc-sections -o $@ cache_footprint.o ufs_namei.test.o

cache_footprint.linear.o: cache_footprint.c
	${HOST_CC} ${HOST_CFLAGS} ${LINEAR_CFLAGS} -c -o $@ ${.ALLSRC}

ufs_namei.linear.test.o: ${TOPSRC}/sys/kern/ufs_namei.c
	${HOST_CC} ${HOST_CFLAGS} ${LINEAR_CFLAGS} -Wno-sign-compare \
	    -c -o $@ ${.ALLSRC}

cache_footprint_linear: cache_footprint.linear.o ufs_namei.linear.test.o
	${HOST_CC} -Wl,--gc-sections -o $@ cache_footprint.linear.o \
	    ufs_namei.linear.test.o

cache_footprint.modulo.o: cache_footprint.c
	${HOST_CC} ${HOST_CFLAGS} -DNCHHASH=3 -c -o $@ ${.ALLSRC}

ufs_namei.modulo.test.o: ${TOPSRC}/sys/kern/ufs_namei.c
	${HOST_CC} ${HOST_CFLAGS} -Wno-sign-compare -DNCHHASH=3 \
	    -c -o $@ ${.ALLSRC}

cache_footprint_modulo: cache_footprint.modulo.o ufs_namei.modulo.test.o
	${HOST_CC} -Wl,--gc-sections -o $@ cache_footprint.modulo.o \
	    ufs_namei.modulo.test.o

cache_footprint.large_hash.o: cache_footprint.c
	${HOST_CC} ${HOST_CFLAGS} -DNCHHASH=37 -c -o $@ ${.ALLSRC}

ufs_namei.large_hash.test.o: ${TOPSRC}/sys/kern/ufs_namei.c
	${HOST_CC} ${HOST_CFLAGS} -Wno-sign-compare -DNCHHASH=37 \
	    -c -o $@ ${.ALLSRC}

cache_footprint_large_hash: cache_footprint.large_hash.o \
    ufs_namei.large_hash.test.o
	${HOST_CC} -Wl,--gc-sections -o $@ cache_footprint.large_hash.o \
	    ufs_namei.large_hash.test.o

check-invalid-config:
	@if ${HOST_CC} ${HOST_CFLAGS} -DBUFHSZ=3 -c invalid_cache_config.c \
	    -o invalid_bufhsz.o 2> invalid_bufhsz.stderr; then \
		echo "BUFHSZ=3 unexpectedly compiled" >&2; exit 1; \
	fi
	grep -F 'BUFHSZ must be a positive power of two' invalid_bufhsz.stderr
	@if ${HOST_CC} ${HOST_CFLAGS} -DNCHHASH=0 -c invalid_cache_config.c \
	    -o invalid_nchhash.o 2> invalid_nchhash.stderr; then \
		echo "NCHHASH=0 unexpectedly compiled" >&2; exit 1; \
	fi
	grep -F 'NCHHASH must be positive' invalid_nchhash.stderr
	@if ${HOST_CC} ${HOST_CFLAGS} -Wno-sign-compare -DNNAMECACHE=1 \
	    -c ${TOPSRC}/sys/kern/ufs_namei.c -o invalid_namecache.o \
	    2> invalid_namecache.stderr; then \
		echo "NNAMECACHE=1 unexpectedly compiled" >&2; exit 1; \
	fi
	grep -F 'NNAMECACHE must provide at least two entries' \
	    invalid_namecache.stderr
	@if ${HOST_CC} ${HOST_CFLAGS} -DSINGLE_UFS_ROOT -DNMOUNT=2 \
	    -c invalid_cache_config.c -o invalid_single_root.o \
	    2> invalid_single_root.stderr; then \
		echo "SINGLE_UFS_ROOT with NMOUNT=2 unexpectedly compiled" >&2; \
		exit 1; \
	fi
	grep -F 'SINGLE_UFS_ROOT requires NMOUNT=1' \
	    invalid_single_root.stderr

check: cache_footprint cache_footprint_linear cache_footprint_modulo \
    cache_footprint_large_hash \
    check-invalid-config
	./cache_footprint
	./cache_footprint_linear
	./cache_footprint_modulo
	./cache_footprint_large_hash

all: check

clean:
	rm -f cache_footprint cache_footprint_linear cache_footprint_modulo \
	    cache_footprint_large_hash \
	    *.o *.stderr

.PHONY: all check check-invalid-config clean
