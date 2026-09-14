# Shared host mapping for little-endian MIPS kernel, userland and host fixtures.
_DEFAULT_MIPS_GCCPREFIX!=if [ x"${_HOST_OSNAME}" = x"OpenBSD" ] ; then \
		echo "/usr/local/bin/mips-elf" ; \
	elif [ x"${_HOST_OSNAME}" = x"FreeBSD" ] ; then \
		echo "/usr/local/mips-elf/bin/mips-elf" ; \
	elif [ x"${_HOST_OSNAME}" = x"Linux" ] ; then \
		echo "mipsel-elf" ; \
	else \
		echo "/does/not/exist" ; \
	fi
MIPS_GCCPREFIX?=	${_DEFAULT_MIPS_GCCPREFIX}
