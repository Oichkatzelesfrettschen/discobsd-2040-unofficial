#
# Kernels live in subdirectories, and have Makefiles of their own.
#
SUBDIR=		PICO PICO_UART
TOPSRC!=	cd ../../../..; pwd
DIVIDER_OWNERSHIP_VERIFIER=	${TOPSRC}/tools/verify_rp2040_divider_ownership.py

default:

all:
		-for i in ${SUBDIR}; do ${MAKE} -C $$i all; done

install:

clean:
		-for i in ${SUBDIR}; do ${MAKE} -C $$i clean; done

clean-all:
		-for i in ${SUBDIR}; do ${MAKE} -C $$i clean-all; done

reconfig:
		-for i in ${SUBDIR}; do ${MAKE} -C $$i reconfig; done

check-divider:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the interpreter for check-divider" >&2; \
			exit 2; \
		fi
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		"${PYTHON}" "${DIVIDER_OWNERSHIP_VERIFIER}" \
		    --elf PICO/unix.elf --dependency-directory PICO/.deps \
		    --elf PICO_UART/unix.elf --dependency-directory PICO_UART/.deps

.PHONY:		default all install clean clean-all reconfig check-divider
