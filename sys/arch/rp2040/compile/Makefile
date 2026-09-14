#
# Kernels live in subdirectories, and have Makefiles of their own.
#
SUBDIR=		PICO PICO_UART
TOPSRC!=	cd ../../../..; pwd
DIVIDER_OWNERSHIP_VERIFIER=	${TOPSRC}/tools/verify_rp2040_divider_ownership.py
SWAPRAM_LINK_VERIFIER=	${TOPSRC}/tools/verify_rp2040_swapram_link.sh
CACHE_FOOTPRINT_VERIFIER=	${TOPSRC}/tools/verify_rp2040_cache_footprint.sh
CACHE_FOOTPRINT_TEST=	${TOPSRC}/tests/rp2040/cache_footprint

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

# Each kernel's PARAM decides whether vm_swap.o calls the SwapRAM tier and
# how large its pool is; the verifier reads the linked kernel and the
# object and refuses a kernel whose bytes disagree with its Config.
check-swapram:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		@set -e; for i in ${SUBDIR}; do \
			param=$$(${MAKE} -C $$i -V PARAM); \
			sh "${SWAPRAM_LINK_VERIFIER}" --elf $$i/unix.elf \
			    --object $$i/vm_swap.o --param "$$param"; \
		done

check-cache-footprint:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		@set -e; for i in ${SUBDIR}; do \
			nm_command=$$(${MAKE} -C $$i -V ARM_GCC_PREFIX)-nm; \
			NM="$$nm_command" sh "${CACHE_FOOTPRINT_VERIFIER}" \
			    --elf $$i/unix.elf; \
		done
		${MAKE} -C "${CACHE_FOOTPRINT_TEST}" check \
		    KERNEL_BUILD="${.CURDIR}/PICO"

.PHONY:		default all install clean clean-all reconfig check-divider \
		check-swapram check-cache-footprint
