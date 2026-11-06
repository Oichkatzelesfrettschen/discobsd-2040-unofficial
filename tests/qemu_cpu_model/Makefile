# Calibrates the qemu-arm CPU model that the qemu tier runs on
# (QEMU_ARM_CPU in tools/qemu-arm.mk). A gate that runs Thumb-1 test programs
# on a model accepting Thumb-2 and unaligned accesses establishes less than
# one that runs them on ARMv6-M, so this directory executes a known-good and
# two known-bad programs on both the tier's model and qemu-arm's permissive
# "max" model:
#
#   baseline   exits 0 on both models
#   thumb2     movw: SIGILL on the tier's model, exits 0 on max
#   unaligned  ldr from an odd address: SIGBUS on the tier's model, exits 0
#              on max
#
# The max rows are the positive control: they show the program executes the
# operation, so the fault on the tier's model is the model's and not the
# program's. REQUIRE_QEMU makes a missing qemu-arm an error; otherwise the
# target prints SKIP and no ARM execution has occurred.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

CONTROL_FLAGS=	-std=gnu17 -Wall -Wextra -Werror -Os ${QEMU_ARM_CPUFLAGS} \
		-ffreestanding -nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-Ttext=0x40000000
CONTROLS=	baseline thumb2 unaligned

all: check

.for c in ${CONTROLS}
control_${c}: control.c
	${QEMU_ARM_CC} ${CONTROL_FLAGS} -DCONTROL_${c:tu} -o $@ control.c
.endfor

check:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "qemu cpu model: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP qemu cpu model: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${CONTROLS:S/^/control_/}
	@sh ./check.sh "${QEMU_ARM}" ${QEMU_ARM_CPU}
.endif

clean:
	rm -f ${CONTROLS:S/^/control_/}

.PHONY: all check clean
