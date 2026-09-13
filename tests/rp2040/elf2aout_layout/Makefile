TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

VERIFIER=       ${.CURDIR}/verify_layout.py

check-python:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for elf2aout layout tests" >&2; \
		exit 2; \
	fi

check: check-python
	test -x ${ELF2AOUT}
	"${PYTHON}" "${VERIFIER}" \
	    --cc "${GCCPREFIX}-gcc" \
	    --ld "${GCCPREFIX}-ld" \
	    --linker-script "${TOPSRC}/lib/elf32-arm.ld" \
	    --elf2aout "${ELF2AOUT}"

all: check

.PHONY: all check check-python
