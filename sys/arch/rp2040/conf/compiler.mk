# GCC arm-none-eabi toolchain: the prefix share/mk/arm-toolchain.mk finds.
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

include		${TOPSRC}/share/mk/arm-toolchain.mk
ARM_GCC_PREFIX=	${ARM_TOOLCHAIN_PREFIX}

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
