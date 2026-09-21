# GCC toolchain shared with MIPS userland and host fixtures.

_HOST_OSNAME!=	uname -s
include ${_confdir}/../../../../share/mk/mips-toolchain.mk

_AS=		-gcc
_CC=		-gcc
_CPP=		-cpp
_LD=		-ld
_SIZE=		-size
_OBJCOPY=	-objcopy
_OBJDUMP=	-objdump

AS=		${MIPS_GCCPREFIX}${_AS}
CC=		${MIPS_GCCPREFIX}${_CC}
CPP=		${MIPS_GCCPREFIX}${_CPP}
LD=		${MIPS_GCCPREFIX}${_LD}
SIZE=		${MIPS_GCCPREFIX}${_SIZE}
OBJCOPY=	${MIPS_GCCPREFIX}${_OBJCOPY}
OBJDUMP=	${MIPS_GCCPREFIX}${_OBJDUMP}
