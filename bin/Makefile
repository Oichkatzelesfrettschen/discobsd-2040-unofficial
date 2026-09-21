#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR=		date2 hostname ls md5 sh stty test \
		cat chgrp chmod cp dd df echo ed expr kill ln mkdir mv ps \
		pwd rm rmail rmdir sleep sync tar

# TODO: csh

all:		${SUBDIR}

${SUBDIR}:	FRC
		${MAKE} -C $@ ${MFLAGS}
# A subdirectory is a directory that exists, so without this bmake judges
# it by mtime against FRC, which is stamped with the second the make began:
# a directory the preceding clean touched in that same second compares as
# up to date and is skipped, which is how lib/crt0.o went missing from a
# clean-then-build with lib/startup-arm never entered.
.PHONY:	${SUBDIR}

FRC:

install:
		for i in ${SUBDIR}; do \
			${MAKE} -C $$i ${MFLAGS} DESTDIR=${DESTDIR} install || exit 1; done

clean:
		status=0; /bin/rm -f a.out core *.s *.o *.dis *.elf *~ \
		    y.tab.[ch] errs || status=1; \
		for i in ${SUBDIR}; do \
			(cd $$i && ${MAKE} ${MFLAGS} clean) || status=1; \
		done; exit $$status
