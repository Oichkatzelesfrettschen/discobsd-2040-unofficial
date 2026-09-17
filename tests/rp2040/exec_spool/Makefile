TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc
KERNEL_BUILD?= ${TOPSRC}/sys/arch/rp2040/compile/PICO
KERNEL_INCLUDES= -I${KERNEL_BUILD} -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
	-I${TOPSRC}/sys/arch/arm/include \
	-I${TOPSRC}/sys/arch/rp2040/dhara/compat \
	-I${TOPSRC}/sys/arch/rp2040/heatshrink \
	-I${TOPSRC}/sys/arch/rp2040/heatshrink/compat
# Dead-section stripping is spelled per linker: GNU ld's and Apple's.
GC_SECTIONS!=	if [ "$$(uname -s)" = Darwin ] ; then echo -Wl,-dead_strip ; else echo -Wl,--gc-sections ; fi
HOST_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -DKERNEL -DSWAPRAM -D__thumb__ \
	-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-sign-compare \
	-Wno-asm-operand-widths \
	${KERNEL_INCLUDES}

# The reservation the spool makes is the kernel's own. libc owns malloc and
# mfree in a host binary, so sys/kern/subr_rmap.c compiles with those names
# and its three companions moved aside; exec_spool_test.c declares the moved
# names and wraps the two it needs.
RMAP_SRC=	${TOPSRC}/sys/kern/subr_rmap.c
RMAP_RENAMES=	-Dmalloc=rmap_malloc -Dmfree=rmap_mfree \
		-Dmalloc3=rmap_malloc3 \
		-Dmalloc3_contiguous=rmap_malloc3_contiguous \
		-Dmalloc3_contiguous_next=rmap_malloc3_contiguous_next

# sys/kern still defines functions the old way, which clang rejects by
# default and the arm-none-eabi gcc the kernel is built with does not.
RMAP_WARN=	-Wno-deprecated-non-prototype

subr_rmap.o: ${RMAP_SRC}
	${HOST_CC} ${HOST_CFLAGS} ${RMAP_WARN} ${RMAP_RENAMES} -c -o $@ ${RMAP_SRC}

exec_spool_test: exec_spool_test.c subr_rmap.o
	${HOST_CC} ${HOST_CFLAGS} ${GC_SECTIONS} -o $@ \
	    exec_spool_test.c subr_rmap.o

check: exec_spool_test
	./exec_spool_test

clean:
	rm -f exec_spool_test subr_rmap.o

.PHONY: check clean
