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

# Local multicall members finish before a box recipe cleans and rebuilds their
# source directories. The dependencies keep inherited -j builds deterministic.
box: chown
sysbox: mknod mount umount
adminbox: reboot shutdown sysctl

FRC:

install: FRC
	-for i in ${SUBDIR}; do \
		(cd $$i; ${MAKE} ${MFLAGS} DESTDIR=${DESTDIR} install); done

clean:
	for i in ${SUBDIR}; do (cd $$i; ${MAKE} ${MFLAGS} clean); done
	rm -f *~
