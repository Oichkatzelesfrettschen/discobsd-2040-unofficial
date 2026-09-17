# Runs the u-area exchange loop of arch/rp2040/rp2040/locore.S under
# qemu-arm and checks every exchanged byte. loop.S is cut from locore.S
# between its uarea-exchange markers at build time, so the harness
# assembles the kernel's lines rather than a copy that could drift.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

LOCORE=		${TOPSRC}/sys/arch/rp2040/rp2040/locore.S
QEMU?=		qemu-arm

loop.S: ${LOCORE}
	sed -n '/uarea-exchange-begin/,/uarea-exchange-end/p' ${LOCORE} > $@
	@grep -q ldmia $@ || { echo "loop.S: no exchange loop cut from locore.S" >&2; rm -f $@; exit 1; }

harness: harness.S loop.S
	${GCCPREFIX}-gcc -x assembler-with-cpp -mcpu=cortex-m0plus -mthumb \
	    -nostdlib -nostartfiles -static -Wl,--build-id=none -o $@ harness.S

# qemu-user writes the guest core itself, into the guest process's working
# directory, so the host's kernel.core_pattern never sees it. The harness
# faults whenever the loop under test overruns, which is the failure the
# test exists to catch, so the run happens with cores/ as the working
# directory and every dump lands there instead of in the source tree.
check: harness
	@if ! command -v ${QEMU} >/dev/null 2>&1; then \
		echo "${QEMU} is required for the u-area exchange test" >&2; \
		exit 2; \
	fi
	@mkdir -p cores
	@cd cores && ${QEMU} ../harness || { \
		status=$$?; \
		echo "u-area exchange failed: harness exit $$status" >&2; \
		echo "1 byte mismatch, 2 pointer overrun, 128+n killed by signal n" >&2; \
		exit 1; \
	}
	@echo "u-area exchange ok"

all: check

clean:
	rm -rf harness loop.S cores

.PHONY: all check clean
