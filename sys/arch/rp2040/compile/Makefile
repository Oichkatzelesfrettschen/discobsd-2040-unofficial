#
# Kernels live in subdirectories, and have Makefiles of their own.
#
TOPSRC!=	cd ../../../..; pwd
MACHINE_DEFAULT=	rp2040
include ${TOPSRC}/share/mk/architecture.mk

.if ${MACHINE} != "rp2040"
.error rp2040 kernel aggregation requires MACHINE=rp2040
.endif

SUBDIR=		${MACHINE_KERNEL_CONFIGS}
DIVIDER_OWNERSHIP_VERIFIER=	${TOPSRC}/tools/verify_rp2040_divider_ownership.py
SWAPRAM_LINK_VERIFIER=	${TOPSRC}/tools/verify_rp2040_swapram_link.sh
CACHE_FOOTPRINT_VERIFIER=	${TOPSRC}/tools/verify_rp2040_cache_footprint.sh
UFS_PROTOTYPE_VERIFIER=	${TOPSRC}/tools/verify_ufs_prototypes.sh
FLASH_SWAP_LINK_VERIFIER=	${TOPSRC}/tools/verify_rp2040_flash_swap_link.sh
CACHE_FOOTPRINT_TEST=	${TOPSRC}/tests/rp2040/cache_footprint
EXEC_SPOOL_TEST=	${TOPSRC}/tests/rp2040/exec_spool
SWAPRAM_TEST=	${TOPSRC}/sys/arch/rp2040/test/swapram

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

check-divider:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the interpreter for check-divider" >&2; \
			exit 2; \
		fi
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		"${PYTHON}" "${DIVIDER_OWNERSHIP_VERIFIER}" \
		    --elf PICO/unix.elf --dependency-directory PICO/.deps \
		    --elf PICO_UART/unix.elf --dependency-directory PICO_UART/.deps \
		    --elf PICOCALC/unix.elf --dependency-directory PICOCALC/.deps \
		    --elf PICOCALC_PSRAM/unix.elf \
		    --dependency-directory PICOCALC_PSRAM/.deps \
		    --elf PICO_DIAG/unix.elf --dependency-directory PICO_DIAG/.deps

# Each kernel's PARAM decides whether vm_swap.o calls the SwapRAM tier and
# how large its pool is; the verifier reads the linked kernel and the
# object and refuses a kernel whose bytes disagree with its Config.
check-swapram:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		@set -e; for i in ${SUBDIR}; do \
			param=$$(${MAKE} -C $$i -V PARAM); \
			sh "${SWAPRAM_LINK_VERIFIER}" --elf $$i/unix.elf \
			    --object $$i/vm_swap.o \
			    --exec-object $$i/exec_hsaout.o \
			    --sysctl-object $$i/kern_sysctl.o --param "$$param"; \
		done

check-cache-footprint:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		@set -e; for i in ${SUBDIR}; do \
			nm_command=$$(${MAKE} -C $$i -V ARM_GCC_PREFIX)-nm; \
			param=$$(${MAKE} -C $$i -V PARAM); \
			layout=multi; \
			case " $$param " in *" -DSINGLE_UFS_ROOT "*) layout=single;; esac; \
			NM="$$nm_command" sh "${CACHE_FOOTPRINT_VERIFIER}" \
			    --elf $$i/unix.elf --layout $$layout; \
		done
		@# Calibration: each layout must reject the other kernel.
		@set -e; nm_command=$$(${MAKE} -C PICO -V ARM_GCC_PREFIX)-nm; \
		if NM="$$nm_command" sh "${CACHE_FOOTPRINT_VERIFIER}" \
		    --elf PICO/unix.elf --layout multi >/dev/null 2>&1; then \
			echo "cache footprint verifier accepted PICO as multi-mount" >&2; exit 1; \
		fi; \
		if NM="$$nm_command" sh "${CACHE_FOOTPRINT_VERIFIER}" \
		    --elf PICOCALC/unix.elf --layout single >/dev/null 2>&1; then \
			echo "cache footprint verifier accepted PICOCALC as single-root" >&2; exit 1; \
		fi
		${MAKE} -C "${CACHE_FOOTPRINT_TEST}" check \
		    KERNEL_BUILD="${.CURDIR}/PICO"
		${MAKE} -C "${EXEC_SPOOL_TEST}" check \
		    KERNEL_BUILD="${.CURDIR}/PICO"
		${MAKE} -C "${SWAPRAM_TEST}" evac

check-exec-spool:
		${MAKE} -C PICO all
		${MAKE} -C "${EXEC_SPOOL_TEST}" check \
		    KERNEL_BUILD="${.CURDIR}/PICO"
		${MAKE} -C "${SWAPRAM_TEST}" evac

check-ufs-prototypes:
		@set -e; for i in ${SUBDIR}; do \
			MAKE="${MAKE}" sh "${UFS_PROTOTYPE_VERIFIER}" \
			    "${.CURDIR}/$$i"; \
		done

check-flash-swap:
		@set -e; for i in ${SUBDIR}; do ${MAKE} -C $$i all; done
		@set -e; for i in ${SUBDIR}; do \
			tool_prefix=$$(${MAKE} -C $$i -V ARM_GCC_PREFIX); \
			swapram_arg=; \
			if [ -r $$i/swapram.o ]; then \
				swapram_arg="--swapram-object $$i/swapram.o"; \
			fi; \
			NM="$${tool_prefix}-nm" OBJDUMP="$${tool_prefix}-objdump" \
			    sh "${FLASH_SWAP_LINK_VERIFIER}" --elf $$i/unix.elf \
			    --flash-object $$i/flash.o \
			    --vm-swap-object $$i/vm_swap.o \
			    --exec-object $$i/exec_subr.o \
			    --swap-object $$i/swap.o $$swapram_arg; \
		done
		@# Reject the retired SRAM5 placement using an otherwise linked kernel.
		@set -eu; work=$$(mktemp -d); trap 'rm -r "$$work"' EXIT HUP INT TERM; \
		tool_prefix=$$(${MAKE} -C PICO_UART -V ARM_GCC_PREFIX); \
		"$${tool_prefix}-objcopy" --strip-symbol flscratch \
		    --add-symbol flscratch=0x20041f00,global \
		    PICO_UART/unix.elf "$$work/old-layout.elf"; \
		if NM="$${tool_prefix}-nm" OBJDUMP="$${tool_prefix}-objdump" \
		    sh "${FLASH_SWAP_LINK_VERIFIER}" --elf "$$work/old-layout.elf" \
		    --flash-object PICO_UART/flash.o --vm-swap-object PICO_UART/vm_swap.o \
		    --exec-object PICO_UART/exec_subr.o --swap-object PICO_UART/swap.o \
		    >"$$work/output" 2>&1; then \
			echo "flash verifier accepted retired SRAM5 placement" >&2; exit 1; \
		fi; \
		grep -F 'flscratch is at 0x20041f00, expected 0x20040500' "$$work/output"

.PHONY:		default all install clean clean-all reconfig check-divider \
		check-swapram check-cache-footprint check-exec-spool \
		check-ufs-prototypes \
		check-flash-swap
