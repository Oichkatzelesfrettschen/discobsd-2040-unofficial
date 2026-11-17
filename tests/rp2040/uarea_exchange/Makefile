# Runs the u-area exchange loop of arch/rp2040/rp2040/locore.S under
# qemu-arm on the ARMv6-M CPU model of tools/qemu-arm.mk and checks every
# exchanged byte. loop.S is cut from locore.S
# between its uarea-exchange markers at build time, so the harness
# assembles the kernel's lines rather than a copy that could drift.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

LOCORE=		${TOPSRC}/sys/arch/rp2040/rp2040/locore.S
STM32_LOCORE=	${TOPSRC}/sys/arch/stm32/stm32/locore.S
CONTEXT_FLAGS=	-x assembler-with-cpp -mthumb -mfloat-abi=soft -I${TOPSRC}/sys \
		-nostdlib -nostartfiles -static -Wl,--build-id=none
CONTEXT_DEPS=	context.S ${TOPSRC}/sys/sys/proc_asm.h

loop.S: ${LOCORE}
	sed -n '/uarea-exchange-begin/,/uarea-exchange-end/p' ${LOCORE} > $@
	@grep -q ldmia $@ || { echo "loop.S: no exchange loop cut from locore.S" >&2; rm -f $@; exit 1; }

harness: harness.S loop.S
	${GCCPREFIX}-gcc -x assembler-with-cpp -mcpu=cortex-m0plus -mthumb \
	    -nostdlib -nostartfiles -static -Wl,--build-id=none -o $@ harness.S

context-rp2040: ${CONTEXT_DEPS} ${LOCORE}
	${GCCPREFIX}-gcc ${CONTEXT_FLAGS} -mcpu=cortex-m0plus -o $@ context.S ${LOCORE}

context-stm32: ${CONTEXT_DEPS} ${STM32_LOCORE}
	${GCCPREFIX}-gcc ${CONTEXT_FLAGS} -mcpu=cortex-m4 -o $@ context.S ${STM32_LOCORE}

context-bad.S: ${LOCORE}
	sed '/Transfer back to setjmp()/s/#1/#2/' ${LOCORE} > $@.tmp
	mv $@.tmp $@

context-bad: ${CONTEXT_DEPS} context-bad.S
	${GCCPREFIX}-gcc ${CONTEXT_FLAGS} -mcpu=cortex-m0plus -o $@ context.S context-bad.S

# qemu-user writes the guest core itself, into the guest process's working
# directory, so the host's kernel.core_pattern never sees it. The harness
# faults whenever the loop under test overruns, which is the failure the
# test exists to catch, so the run happens with cores/ as the working
# directory and every dump lands there instead of in the source tree.
check: harness context-rp2040 context-stm32 context-bad
.if empty(QEMU_ARM)
	@echo "qemu-arm is required for the u-area exchange test" >&2; exit 2
.endif
	@mkdir -p cores
	@cd cores && ${QEMU_ARM_RUN} ../harness || { \
		status=$$?; \
		echo "u-area exchange failed: harness exit $$status" >&2; \
		echo "1 byte mismatch, 2 pointer overrun, 128+n killed by signal n" >&2; \
		exit 1; \
	}
	@echo "u-area exchange ok"
	@cd cores && ${QEMU_ARM_RUN} ../context-rp2040 && \
	    ${QEMU_ARM} -cpu cortex-m4 ../context-stm32
	@cd cores && { status=0; ${QEMU_ARM_RUN} ../context-bad || status=$$?; \
	    if [ "$$status" != 4 ]; then \
		echo "context return-value control: expected 4, got $$status" >&2; exit 1; \
	    fi; }
	@echo "context save/resume passes on RP2040 and STM32 and rejects a wrong return value"

all: check

clean:
	rm -rf harness loop.S cores context-rp2040 context-stm32 context-bad context-bad.S context-bad.S.tmp

.PHONY: all check clean
