#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The native toolchain, as cc cpp lcc lcpp ld smallc aout adb, emits MIPS
# code, so it is built for the machines that carry it today and left out of
# rp2040, whose root cannot hold what it cannot run. smlrc carries a Thumb-1
# back end and is built for rp2040. The peripheral demos are STM32 and PIC32
# hardware.
MDSUBDIR!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "lex smlrc as ld pdp11 mputest" ; \
	elif [ x"${MACHINE_ARCH}" = x"arm" ] ; then \
		echo "lex as cc cpp lcc lcpp ld smallc aout glcdtest gtest portio pwm wiznet smux" ; \
	elif [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "adb smlrc as cc cpp lcc lcpp ld smallc aout glcdtest gtest portio pwm wiznet smux" ; \
	else \
		echo "" ; \
	fi

# picoc pays 67 KB of root for a C interpreter rp2040 does not need: the
# native chain (smlrc, as, ld, distrib/rp2040/cc) compiles and runs a
# program instead of interpreting its source.
_PICOC!=if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "" ; \
	else \
		echo "picoc" ; \
	fi

SUBDIR		= ar arch awk basic calendar chflags chpass dc diff emg env \
		  column find fmt fold forth fstat id kilo la login m4 make man med menu more nm passwd \
		  pdc ${_PICOC} printf ranlib re renice retroforth scm sed setty sl \
		  stevie \
		  uname xargs xinstall zmodem msec compress tip tclsh uucp virus \
		  whereis yacc ${MDSUBDIR} basename cal cb cmp col colrm comm diskspeed du \
		  fgrep file grep head hostid join last mesg nice od pagesize pr \
		  printenv rev size sort split sum tail tee time touch tr tsort tty \
		  uniq unifdef w wc who bc egrep mail su vmstat wall write strip false nohup \
		  true cut paste seq dirname nl cksum expand unexpand mkfifo \
		uuencode uudecode cpio getopt yes strings users

# TODO: ccom lccom pforth sre unixbench adc-demo
#       lorder mkdep

all:		$(SUBDIR)

$(SUBDIR):	FRC
		$(MAKE) -C $@ $(MFLAGS)
# A subdirectory is a directory that exists, so without this bmake judges
# it by mtime against FRC, which is stamped with the second the make began:
# a directory the preceding clean touched in that same second compares as
# up to date and is skipped, which is how lib/crt0.o went missing from a
# clean-then-build with lib/startup-arm never entered.
.PHONY:	$(SUBDIR)

FRC:

install:
		for i in $(SUBDIR); do \
			$(MAKE) -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install || exit 1; done

clean:
		/bin/rm -f a.out core *.s *.o *.dis *.elf *~ y.tab.[ch] errs
		for i in $(SUBDIR); do (cd $$i; $(MAKE) $(MFLAGS) clean); done
