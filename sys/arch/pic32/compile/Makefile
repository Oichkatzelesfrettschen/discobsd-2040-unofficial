#
# Kernels live in subdirectories, and have Makefiles of their own.
#
SUBDIR=		BAREMETAL DUINOMITE DUINOMITE_EMEGA EXPLORER16 \
		FUBARINO MAX32 MAXIMITE MAXIMITE_COLOR MMB_MX7 \
		PICADILLO PINGUINO_MICRO QUICK240 \
		SDXL SNADPIC STARTER_KIT UBW32 WF32

default:

all:
		set -eu; for kernel_config in ${SUBDIR}; do \
			${MAKE} -C $$kernel_config all; done

install:

clean:
		set -eu; for kernel_config in ${SUBDIR}; do \
			${MAKE} -C $$kernel_config clean; done

clean-all:
		set -eu; for kernel_config in ${SUBDIR}; do \
			${MAKE} -C $$kernel_config clean-all; done

reconfig:
		set -eu; for kernel_config in ${SUBDIR}; do \
			${MAKE} -C $$kernel_config reconfig; done
