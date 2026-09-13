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

check: harness
	@if ! command -v ${QEMU} >/dev/null 2>&1; then \
		echo "${QEMU} is required for the u-area exchange test" >&2; \
		exit 2; \
	fi
	${QEMU} ./harness; echo "harness exit $$?"; ${QEMU} ./harness

all: check

clean:
	rm -f harness loop.S

.PHONY: all check clean
