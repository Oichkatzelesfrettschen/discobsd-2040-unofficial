#
# Public domain - 1996/10/24 - sms
#
#	@(#)Makefile	1.2 (2.11BSD) 1997/6/27
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR=	chroot cron disktool iostat mkpasswd pstat talloc \
	update vipw

# TODO: devupdate

all:	${SUBDIR}

${SUBDIR}: FRC
	cd $@; ${MAKE} ${MFLAGS} all
# A subdirectory is a directory that exists, so without this bmake judges
# it by mtime against FRC, which is stamped with the second the make began:
# a directory the preceding clean touched in that same second compares as
# up to date and is skipped, which is how lib/crt0.o went missing from a
# clean-then-build with lib/startup-arm never entered.
.PHONY:	${SUBDIR}

FRC:

install: FRC
	for i in ${SUBDIR}; do \
		(cd $$i; ${MAKE} ${MFLAGS} DESTDIR=${DESTDIR} install) || exit 1; done

clean:
	status=0; /bin/rm -f a.out core *.s *.o *.dis *.elf *~ errs || status=1; \
	for i in ${SUBDIR}; do \
		(cd $$i && ${MAKE} ${MFLAGS} clean) || status=1; \
	done; exit $$status
