TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOST_CC?= cc

PORT_INCLUDE=	${TOPSRC}/sys/arch/${MACHINE}/include

# sys/param.h reaches a port's headers as <machine/*.h>. config(8) spells that
# name as a symlink in the kernel build directory, which a gate that has to
# run in an unconfigured tree cannot rely on; this tier generates one
# forwarding header per port header instead, so it needs neither a cross build
# nor a name outside the tree.
MACHINE_STAMP=	machine/.stamp

# Kernel sources reach libc's namespace when they link into a host binary.
# printf() would take over every libc call to that name, and sys/kern's
# malloc() and mfree() take a resource map where libc's take a byte count, so
# the four names move aside. hostkern_kern.c compiles with this same set and
# includes both the harness header and the kernel's, which is what holds the
# harness to the signatures the kernel declares.
KERNEL_RENAMES=	-Dprintf=hk_kprintf \
		-Dmalloc=rmap_malloc -Dmfree=rmap_mfree \
		-Dmalloc3=rmap_malloc3 \
		-Dmalloc3_contiguous=rmap_malloc3_contiguous \
		-Dmalloc3_contiguous_next=rmap_malloc3_contiguous_next

# -ffreestanding and -fno-builtin are what sys/arch/${MACHINE}/conf builds the
# kernel with, and they are also what keeps the compiler from holding
# sys/systm.h's declarations of printf, log and strncmp against its own.
KERN_BASE=	-std=gnu17 -ffreestanding -fno-builtin \
		-DKERNEL -I. -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
		${KERNEL_RENAMES}

# A kernel source is held to the warning set the kernel is built with, and no
# more: a gate that rejected code the production build accepts would fail for
# something that ships. The gate's own sources take the stricter set.
KERN_WARN=	-Wall -Werror
GATE_WARN=	-Wall -Wextra -Werror

KERN_CFLAGS=	${KERN_BASE} ${KERN_WARN}
GATE_CFLAGS=	${KERN_BASE} ${GATE_WARN}
HARNESS_CFLAGS=	-std=gnu17 ${GATE_WARN}

RMAP_SRC=	${TOPSRC}/sys/kern/subr_rmap.c

# The resource map allocator runs in three shapes across the tree's ports:
# the sixteen-bit descriptors sys/arch/rp2040/compile/PICO configures, the
# wide descriptors stm32 and pic32 use, and a DIAGNOSTIC kernel, where
# mfree()'s three overlap panics compile in.
COMPACT_DEFS=		-DCOMPACT_SWAPMAP
WIDE_DEFS=
DIAGNOSTIC_DEFS=	-DCOMPACT_SWAPMAP -DDIAGNOSTIC

PROGS=	rmap_test_compact rmap_test_wide rmap_test_diagnostic

all: ${PROGS}

${MACHINE_STAMP}:
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/include/$$name>" > machine/$$name; \
	done
	@touch $@

# The harness carries no kernel header, so it compiles without the renames
# and reaches <setjmp.h>, <stdio.h> and <string.h> the way any host program
# does. hostkern_kern.c is the opposite case and builds once per shape,
# because the kernel's own headers decide its types.
hostkern.o: hostkern.c hostkern.h
	${HOST_CC} ${HARNESS_CFLAGS} -c -o $@ hostkern.c

rmap_test_compact: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${COMPACT_DEFS} -c -o rmap_kernel_compact.o \
	    ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${COMPACT_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_compact.o hostkern.o

rmap_test_wide: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${WIDE_DEFS} -c -o rmap_kernel_wide.o \
	    ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${WIDE_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_wide.o hostkern.o

rmap_test_diagnostic: rmap_test.c ${RMAP_SRC} hostkern_kern.c hostkern.o \
    hostkern.h ${MACHINE_STAMP}
	${HOST_CC} ${KERN_CFLAGS} ${DIAGNOSTIC_DEFS} \
	    -c -o rmap_kernel_diagnostic.o ${RMAP_SRC}
	${HOST_CC} ${GATE_CFLAGS} ${DIAGNOSTIC_DEFS} -o $@ \
	    rmap_test.c hostkern_kern.c rmap_kernel_diagnostic.o hostkern.o

check: ${PROGS}
	@for prog in ${PROGS}; do \
		echo "run ./$$prog"; \
		./$$prog || exit 1; \
	done

clean:
	rm -f ${PROGS} hostkern.o
	rm -f rmap_kernel_compact.o rmap_kernel_wide.o rmap_kernel_diagnostic.o
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

.PHONY: all check clean
