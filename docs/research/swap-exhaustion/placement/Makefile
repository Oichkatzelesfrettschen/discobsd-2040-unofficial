# Host build of the raw-swap placement study.  The PICO kernel build supplies
# the machine headers, so run `bmake MACHINE=rp2040 logged-kernel` first.
TOPSRC!=cd ../../../..; pwd

HOST_CC?= cc
SESSIONS?= 2000
COMMANDS?= 400
KERNEL_BUILD?= ${TOPSRC}/sys/arch/rp2040/compile/PICO
KERNEL_INCLUDES= -I${KERNEL_BUILD} -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
	-I${TOPSRC}/sys/arch/arm/include \
	-I${TOPSRC}/sys/arch/rp2040/dhara/compat
HOST_CFLAGS= -std=gnu17 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-DKERNEL -D__thumb__ -Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast -Wno-sign-compare ${KERNEL_INCLUDES}

# libc owns malloc and mfree in a host binary, so sys/kern/subr_rmap.c
# compiles with its allocator names moved aside.
RMAP_SRC=	${TOPSRC}/sys/kern/subr_rmap.c
RMAP_RENAMES=	-Dmalloc=rmap_malloc -Dmfree=rmap_mfree \
		-Dmalloc3=rmap_malloc3 \
		-Dmalloc3_contiguous=rmap_malloc3_contiguous \
		-Dmalloc3_contiguous_next=rmap_malloc3_contiguous_next

all: placement_sim

subr_rmap.o: ${RMAP_SRC}
	${HOST_CC} ${HOST_CFLAGS} -Wno-deprecated-non-prototype \
	    ${RMAP_RENAMES} -c -o $@ ${RMAP_SRC}

placement_sim: placement_sim.c subr_rmap.o
	${HOST_CC} ${HOST_CFLAGS} -o $@ placement_sim.c subr_rmap.o

# Exit status 1 means the zero-guard search left the production placement.
run: placement_sim
	./placement_sim ${SESSIONS} ${COMMANDS}

clean:
	rm -f placement_sim subr_rmap.o

.PHONY: all run clean
