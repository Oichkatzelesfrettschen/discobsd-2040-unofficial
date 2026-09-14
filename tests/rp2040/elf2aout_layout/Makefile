TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

VERIFIER=       ${.CURDIR}/verify_layout.py
MIPS_GCCPREFIX!=if [ x"${_HOST_OSNAME}" = x"OpenBSD" ]; then \
		echo "/usr/local/bin/mips-elf"; \
	elif [ x"${_HOST_OSNAME}" = x"FreeBSD" ]; then \
		echo "/usr/local/mips-elf/bin/mips-elf"; \
	elif [ x"${_HOST_OSNAME}" = x"Linux" ]; then \
		echo "/usr/local/mips-gcc-4.8.1/bin/mips-elf"; \
	else \
		echo "/does/not/exist"; \
	fi

check-python:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for elf2aout layout tests" >&2; \
		exit 2; \
	fi

check-mips-tools:
	@for tool_name in gcc ld nm objcopy readelf; do \
		command -v ${MIPS_GCCPREFIX}-$$tool_name >/dev/null || { \
			echo "missing ${MIPS_GCCPREFIX}-$$tool_name" >&2; \
			exit 2; \
		}; \
	done

check: check-python check-mips-tools
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
	    --overlay-verifier "${TOPSRC}/tools/verify_multicall_bss_objects.sh" \
	    --mips-cc "${MIPS_GCCPREFIX}-gcc" \
	    --mips-ld "${MIPS_GCCPREFIX}-ld" \
	    --mips-nm "${MIPS_GCCPREFIX}-nm" \
	    --mips-objcopy "${MIPS_GCCPREFIX}-objcopy" \
	    --mips-readelf "${MIPS_GCCPREFIX}-readelf" \
	    --mips-linker-script "${TOPSRC}/lib/elf32-mips.ld"

all: check

.PHONY: all check check-mips-tools check-python
