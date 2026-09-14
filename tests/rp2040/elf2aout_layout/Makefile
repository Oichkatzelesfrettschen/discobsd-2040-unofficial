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
	    --make "${MAKE}" \
	    --nm "${GCCPREFIX}-nm" \
	    --objcopy "${GCCPREFIX}-objcopy" \
	    --readelf "${GCCPREFIX}-readelf" \
	    --linker-script "${TOPSRC}/lib/elf32-arm.ld" \
	    --elf2aout "${ELF2AOUT}" \
	    --overlay-generator "${TOPSRC}/tools/generate_multicall_bss_overlay.sh" \
	    --overlay-verifier "${TOPSRC}/tools/verify_multicall_bss_objects.sh"

all: check

.PHONY: all check check-python
