#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= ${DEFS}

# ARMv6-M has no divide instruction. The RP2040 helpers run on its SIO
# divider, a block no other machine has; the Cortex-M4 machines divide with
# SDIV and UDIV.
.if ${MACHINE} == "rp2040"
DIV_SRC		= aeabi_div.S
.else
DIV_SRC		= aeabi_div_idiv.S
.endif
BASE_SRCS	= _setjmp.S ${DIV_SRC} htonl.S htons.S setjmp.S sigsetjmp.S
BASE_OBJS	= ${BASE_SRCS:.S=.o}
ROM_FLOAT_OBJS!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
			awk 'NF { print $$1 ".o" }' rom_float_members ; \
		fi
ROM_FLOAT_SRCS	= ${ROM_FLOAT_OBJS:.o=.S}
SRCS		= ${BASE_SRCS} ${ROM_FLOAT_SRCS}
OBJS		= ${BASE_OBJS} ${ROM_FLOAT_OBJS}

gen.a:		${OBJS}
		@echo "building gen.a"
		@rm -f gen.a
		@${AR} cr gen.a ${OBJS}

${ROM_FLOAT_OBJS}: rom_float.inc rom_float_members

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

clean:
		rm -f gen.a *.o *~ profiled/*.o tags Makefile.bak

depend:
		for i in ${SRCS}; do \
		    cc -M ${CFLAGS} $$i | awk ' { if ($$1 != prev) \
			{ if (rec != "") print rec; rec = $$0; prev = $$1; } \
			else { if (length(rec $$2) > 78) { print rec; rec = $$0; } \
			else rec = rec " " $$2 } } \
			END { print rec } ' >> makedep; done
		echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
		echo '$$r makedep' >>eddep
		echo 'w' >>eddep
		cp Makefile Makefile.bak
		ed - Makefile < eddep
		rm eddep makedep
		echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile
		echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile
		echo '# see make depend above' >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
