#
# Kernels live in subdirectories, and have Makefiles of their own.
#
TOPSRC!=	cd ../../../..; pwd
MACHINE_DEFAULT=	stm32
include ${TOPSRC}/share/mk/architecture.mk

.if ${MACHINE} != "stm32"
.error stm32 kernel aggregation requires MACHINE=stm32
.endif

SUBDIR=		${MACHINE_KERNEL_CONFIGS}

default:

all:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done

install:

clean:
		status=0; for i in ${SUBDIR}; do \
			${MAKE} -C $$i clean || status=1; done; exit $$status

clean-all:
		status=0; for i in ${SUBDIR}; do \
			${MAKE} -C $$i clean-all || status=1; done; exit $$status

reconfig:
		status=0; for i in ${SUBDIR}; do \
			${MAKE} -C $$i reconfig || status=1; done; exit $$status
