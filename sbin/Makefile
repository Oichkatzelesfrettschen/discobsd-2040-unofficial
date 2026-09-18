#
# Public domain - 1996/10/26 - sms
#
#	@(#)Makefile	1.3 (2.11BSD) 1999/7/21
#
TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

SUBDIR=		chown fdisk fsck init mkfs mknod mount reboot shutdown sysctl umount box sysbox textbox utilbox adminbox grepbox

all:	${SUBDIR}

${SUBDIR}: FRC
	cd $@; ${MAKE} ${MFLAGS} all
# A subdirectory is a directory that exists, so without this bmake judges
# it by mtime against FRC, which is stamped with the second the make began:
# a directory the preceding clean touched in that same second compares as
# up to date and is skipped, which is how lib/crt0.o went missing from a
# clean-then-build with lib/startup-arm never entered.
.PHONY:	${SUBDIR}

# Local multicall members finish before a box recipe cleans and rebuilds their
# source directories. The dependencies keep inherited -j builds deterministic.
box: chown
sysbox: mknod mount umount
adminbox: reboot shutdown sysctl

FRC:

install: FRC
	for i in ${SUBDIR}; do \
		(cd $$i; ${MAKE} ${MFLAGS} DESTDIR=${DESTDIR} install) || exit 1; done

clean:
	for i in ${SUBDIR}; do (cd $$i; ${MAKE} ${MFLAGS} clean); done
	rm -f *~
