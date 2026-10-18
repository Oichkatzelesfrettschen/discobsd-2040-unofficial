# GCC arm-none-eabi toolchain: the one on PATH (Homebrew on macOS), else
# the packaged path on OpenBSD, FreeBSD, and Linux
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

_HOST_OSNAME!=	uname -s

ARM_GCC_PREFIX!=if command -v arm-none-eabi-gcc >/dev/null 2>&1 ; then \
			echo "$$(command -v arm-none-eabi-gcc | sed 's/-gcc$$//')" ; \
		elif [ x"${_HOST_OSNAME}" = x"OpenBSD" ] ; then \
			echo "/usr/local/bin/arm-none-eabi" ; \
		elif [ x"${_HOST_OSNAME}" = x"FreeBSD" ] ; then \
			echo "/usr/local/gcc-arm-embedded/bin/arm-none-eabi" ; \
		elif [ x"${_HOST_OSNAME}" = x"Linux" ] ; then \
			echo "/usr/bin/arm-none-eabi" ; \
		else \
			echo "/does/not/exist" ; \
		fi

_AS=		-gcc
_CC=		-gcc
_CPP=		-cpp
_LD=		-ld
_SIZE=		-size
_OBJCOPY=	-objcopy
_OBJDUMP=	-objdump

C17_CAPTURE_PREFIX=
.if defined(C17_CAPTURE_DIR) && !empty(C17_CAPTURE_DIR)
.if !defined(PYTHON) || empty(PYTHON)
.error C17 capture requires caller-selected PYTHON
.endif
C17_CAPTURE_PREFIX= ${PYTHON} ${TOPSRC}/tools/c17/record_tool.py \
    --root ${TOPSRC} --records ${C17_CAPTURE_DIR} --${:U }
.export C17_CAPTURE_DIR
.endif

AS=		${C17_CAPTURE_PREFIX}${ARM_GCC_PREFIX}${_AS}
CC=		${C17_CAPTURE_PREFIX}${ARM_GCC_PREFIX}${_CC}
CPP=		${C17_CAPTURE_PREFIX}${ARM_GCC_PREFIX}${_CPP}
LD=		${C17_CAPTURE_PREFIX}${ARM_GCC_PREFIX}${_LD}
SIZE=		${ARM_GCC_PREFIX}${_SIZE}
OBJCOPY=	${C17_CAPTURE_PREFIX}${ARM_GCC_PREFIX}${_OBJCOPY}
OBJDUMP=	${ARM_GCC_PREFIX}${_OBJDUMP}
