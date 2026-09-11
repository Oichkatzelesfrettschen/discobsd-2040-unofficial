#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The native toolchain, as cc cpp lcc lcpp ld smallc aout adb smlrc, emits
# MIPS code, so it is built for the machines that carry it today and left
# out of rp2040, whose root cannot hold what it cannot run. The peripheral
# demos are STM32 and PIC32 hardware.
MDSUBDIR!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "lex" ; \
	elif [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		echo "lex as cc cpp lcc lcpp ld smallc aout glcdtest gtest portio pwm wiznet smux" ; \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "adb smlrc as cc cpp lcc lcpp ld smallc aout glcdtest gtest portio pwm wiznet smux" ; \
	else \
		echo "" ; \
	fi

SUBDIR		= ar arch awk basic calendar chflags chpass dc diff emg env \
		  find fold forth fstat id la login m4 make man med more nm passwd \
		  pdc picoc printf ranlib re renice retroforth scm sed setty sl \
		  uname xargs xinstall zmodem msec compress tip tclsh uucp virus \
		  whereis yacc ${MDSUBDIR} basename cal cb cmp col comm diskspeed du \
		  fgrep file grep head hostid join last mesg nice od pagesize pr \
		  printenv rev size sort split sum tail tee time touch tr tsort tty \
		  uniq w wc who bc egrep mail su vmstat wall write strip false nohup \
		  true

# TODO: ccom lccom pforth sre unixbench adc-demo
#       lorder mkdep

all:		$(SUBDIR)

$(SUBDIR):	FRC
		$(MAKE) -C $@ $(MFLAGS)

FRC:

install:
		-for i in $(SUBDIR); do \
			$(MAKE) -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install; done

clean:
		/bin/rm -f a.out core *.s *.o *.dis *.elf *~ y.tab.[ch] errs
		for i in $(SUBDIR); do (cd $$i; $(MAKE) $(MFLAGS) clean); done
