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

# as.c raises three -Wextra groups that the Thumb-1 source does not: 121 of
# optable[]'s 123 entries leave the pseudo-instruction handler to C's rule
# that an omitted initializer is zero, 7 comparisons mix signed and unsigned
# operands, and 6 switch cases fall through. The RP2040 builds as-thumb.c and
# stays at the full set; these counts go when the sites do.
ASWARN!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "" ; \
	else \
		echo "-Wno-missing-field-initializers -Wno-sign-compare -Wno-implicit-fallthrough" ; \
	fi
CFLAGS+=	${ASWARN}
