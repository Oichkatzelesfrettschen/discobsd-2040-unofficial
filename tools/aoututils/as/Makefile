#
# as - assembler
#
# The RP2040 port assembles Thumb-1 and every other machine MIPS, so the
# host tool is built from whichever source usr.bin/as selects.
#

PROG=	as
MACHINE?=	stm32

OBJS!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "as-thumb.o" ; \
	else \
		echo "as.o" ; \
	fi

include	../Makefile.inc
