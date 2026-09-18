#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR=	libc_aout \
	startup-${MACHINE_ARCH} libc libm libcurses libtermlib \
	libutil libvmf libtcl libreadline libgpanel libwiznet

all:		$(SUBDIR)

$(SUBDIR):	FRC
		$(MAKE) -C $@

FRC:

# A library a later program links against is installed by an earlier
# iteration, so the first failed child ends the loop, and the loop's own
# status is the recipe's: bin/Makefile carries the same shape.
install:        elf32-$(MACHINE_ARCH).ld
		for i in $(SUBDIR); do \
			${MAKE} -C $$i DESTDIR=${DESTDIR} install || exit 1; done
#		cp -p elf32-$(MACHINE_ARCH).ld $(DESTDIR)/usr/lib/elf32-$(MACHINE_ARCH).ld

# Cleanup visits every child whatever an earlier one returned, then
# reports the failures it kept, so a partial clean is never a success.
clean:
		rm -f a.out core *.s *.o *.a *~
		status=0; for i in $(SUBDIR); do \
			$(MAKE) -C $$i clean || status=1; done; exit $$status
