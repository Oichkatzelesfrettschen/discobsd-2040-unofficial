#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR          = adventure atc backgammon battlestar boggle btlgammon \
                  caesar cribbage fortune hangman mille monop pom pig quiz \
                  robots rogue sail snake trek \
                  banner aclock arithmetic bcd factor fish morse \
                  number ppt wump primes rain worm worms canfield \
                  fifteen keen bubble gamebox

all:            $(SUBDIR)

$(SUBDIR):      FRC
		$(MAKE) -C $@ $(MFLAGS)

# gamebox cleans and rebuilds its three members. Complete sibling builds first
# so an inherited -j setting cannot remove an object another make is writing.
gamebox:        fifteen keen bubble

FRC:

install:
		-for i in $(SUBDIR); do \
			${MAKE} -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install; done

clean:
		rm -f a.out core *.s *.o *.dis *.elf
		-for i in $(SUBDIR); do ${MAKE} -C $$i $(MFLAGS) clean; done
