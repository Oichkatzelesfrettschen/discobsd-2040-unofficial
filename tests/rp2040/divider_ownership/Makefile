TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

VERIFIER=	${TOPSRC}/tools/verify_rp2040_divider_ownership.py
FIXTURE_CC=	${GCCPREFIX}-gcc
FIXTURE_FLAGS=	-std=gnu17 -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
FIXTURE_FLAGS+=	-ffreestanding -nostdlib -nostartfiles -Wall -Wextra -Werror
FIXTURE_FLAGS+=	-Wa,--fatal-warnings -Wl,--fatal-warnings -Wl,-z,noexecstack
FIXTURE_FLAGS+=	-Wl,--entry=entry -Wl,--section-start=.text=0x10000000
FIXTURE_FLAGS+=	-Wl,--build-id=none
FIXTURE_ELFS=	divider_safe.elf divider_negative.elf \
		divider_control_flow_negative.elf divider_residual_negative.elf
DEPENDENCY_FILE=	.deps/source_negative.dep
COMMENT_DEPENDENCY_FILE=	.deps-comment/source_comment_safe.dep

all: check

check-python:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for divider tests" >&2; \
		exit 2; \
	fi
	@"${PYTHON}" -c 'import capstone, elftools'

check: check-python ${FIXTURE_ELFS} ${DEPENDENCY_FILE} \
	    ${COMMENT_DEPENDENCY_FILE}
	"${PYTHON}" "${VERIFIER}" --elf divider_safe.elf \
	    > safe.stdout 2> safe.stderr
	test ! -s safe.stderr
	grep -F 'sio_access_addresses=0xd0000004,0xd0000014 divider_findings=0' \
	    safe.stdout
	@if "${PYTHON}" "${VERIFIER}" --elf divider_negative.elf \
	    > divider_negative.stdout 2> divider_negative.stderr; then \
		echo "machine-code negative control unexpectedly passed" >&2; \
		exit 1; \
	else \
		status=$$?; test $$status -eq 1; \
	fi
	grep -F 'divider_findings=4' divider_negative.stdout
	grep -F 'ldr r1, [r0, #0x60]: 0xd0000060' divider_negative.stderr
	grep -F '.word 0xd0000078: 0xd0000078' divider_negative.stderr
	@if "${PYTHON}" "${VERIFIER}" --elf divider_control_flow_negative.elf \
	    > divider_control_flow_negative.stdout \
	    2> divider_control_flow_negative.stderr; then \
		echo "control-flow negative control unexpectedly passed" >&2; \
		exit 1; \
	else \
		status=$$?; test $$status -eq 1; \
	fi
	grep -F 'divider_findings=1' divider_control_flow_negative.stdout
	grep -F 'ldr r2, [r0, r1]: 0xd0000060' \
	    divider_control_flow_negative.stderr
	@if "${PYTHON}" "${VERIFIER}" --elf divider_residual_negative.elf \
	    > divider_residual_negative.stdout \
	    2> divider_residual_negative.stderr; then \
		echo "residual-code negative control unexpectedly passed" >&2; \
		exit 1; \
	else \
		status=$$?; test $$status -eq 1; \
	fi
	grep -F 'divider_findings=1' divider_residual_negative.stdout
	grep -F 'ldr r2, [r0, r1]: 0xd0000060' \
	    divider_residual_negative.stderr
	@if "${PYTHON}" "${VERIFIER}" --elf divider_safe.elf \
	    --dependency-directory .deps \
	    > source_negative.stdout 2> source_negative.stderr; then \
		echo "source negative control unexpectedly passed" >&2; \
		exit 1; \
	else \
		status=$$?; test $$status -eq 1; \
	fi
	grep -F 'source_files=1 source_divider_findings=2' source_negative.stdout
	grep -F 'divider symbol: #define DIV_QUOTIENT_OFFSET 0x70' \
	    source_negative.stderr
	"${PYTHON}" "${VERIFIER}" --elf divider_safe.elf \
	    --dependency-directory .deps-comment \
	    > source_comment_safe.stdout 2> source_comment_safe.stderr
	test ! -s source_comment_safe.stderr
	grep -F 'source_files=1 source_divider_findings=0' \
	    source_comment_safe.stdout
	@if "${PYTHON}" "${VERIFIER}" --elf divider_safe.elf \
	    --dependency-directory missing-dependencies \
	    > missing_dependencies.stdout 2> missing_dependencies.stderr; then \
		echo "missing-dependency negative control unexpectedly passed" >&2; \
		exit 1; \
	else \
		status=$$?; test $$status -eq 2; \
	fi
	grep -F 'missing dependency directory' missing_dependencies.stderr

.SUFFIXES: .S .elf

.S.elf:
	${FIXTURE_CC} ${FIXTURE_FLAGS} -o $@ $<

${DEPENDENCY_FILE}: source_negative.c
	mkdir -p .deps
	${FIXTURE_CC} -std=gnu17 -mcpu=cortex-m0plus -mthumb \
	    -mfloat-abi=soft -ffreestanding -nostdinc -M \
	    -MT source_negative.o -MF $@ source_negative.c

${COMMENT_DEPENDENCY_FILE}: source_comment_safe.c
	mkdir -p .deps-comment
	${FIXTURE_CC} -std=gnu17 -mcpu=cortex-m0plus -mthumb \
	    -mfloat-abi=soft -ffreestanding -nostdinc -M \
	    -MT source_comment_safe.o -MF $@ source_comment_safe.c

clean:
	rm -rf .deps .deps-comment *.elf *.stderr *.stdout

.PHONY: all check check-python clean
