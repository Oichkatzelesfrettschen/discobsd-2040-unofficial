#
# Public domain - 1997/1/10 - sms
#
#	@(#)Makefile	1.0 (2.11BSD) 1997/1/10
#
# DESTDIR is defined here to be something which does *NOT* exist - it must be
#  specified on the command line when doing a "make DESTDIR=/mnt distribution".
#  This is aimed at avoiding overwriting the system disk's /etc files.
#
# Example:
#	mount /dev/ra1a /mnt
#	make DESTDIR=/mnt distribution

TOPSRC!=cd ..; pwd
include ${TOPSRC}/share/mk/sys.mk

DESTDIR=	/foobar
RELEASEDIR=	/foobaz
OWN=		root
GRP=		wheel

TZDIR=		/usr/share/zoneinfo
LOCALTIME=	Canada/Mountain

FILES=		COPYRIGHT gettytab group motd rc.local ttys

# usr.bin/menu reads /etc/menu for its list of launchable commands; only
# rp2040 ships the menu program (usr.bin/Makefile's SUBDIR), so only its
# manifest (distrib/rp2040/mi.rp2040) names /etc/menu and only its build
# needs the file installed into DESTDIR.
MDFILES!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
			echo "menu" ; \
		else \
			echo "" ; \
		fi

# A machine whose root is not sd0a, or whose disk is too small for what
# rc starts, carries its own fstab or rc as fstab.${MACHINE} or
# rc.${MACHINE}, installed under the plain name; passwd and shells follow
# the same rule for a root whose home directories or shells differ.
FSTAB!=		if [ -f fstab.${MACHINE} ] ; then \
			echo "fstab.${MACHINE}" ; \
		else \
			echo "fstab" ; \
		fi
PASSWD!=	if [ -f passwd.${MACHINE} ] ; then \
			echo "passwd.${MACHINE}" ; \
		else \
			echo "passwd" ; \
		fi
SHELLS!=	if [ -f shells.${MACHINE} ] ; then \
			echo "shells.${MACHINE}" ; \
		else \
			echo "shells" ; \
		fi
RC!=		if [ -f rc.${MACHINE} ] ; then \
			echo "rc.${MACHINE}" ; \
		else \
			echo "rc" ; \
		fi

all install depend lint tags:

clean:
	rm -f *~

distribution: distribution-etc-root-var

distribution-etc-root-var: distrib-dirs
	${INSTALL} -d -o ${OWN} -g ${GRP} -m 755 ${DESTDIR}/dev
	${INSTALL} -d -o ${OWN} -g ${GRP} -m 755 ${DESTDIR}/etc

	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${FILES} ${DESTDIR}/etc
	if [ -n "${MDFILES}" ] ; then \
		${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${MDFILES} ${DESTDIR}/etc ; \
	fi
	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${FSTAB} ${DESTDIR}/etc/fstab
	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${RC} ${DESTDIR}/etc/rc
	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${PASSWD} ${DESTDIR}/etc/passwd
	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 ${SHELLS} ${DESTDIR}/etc/shells
	${INSTALL} -c -o root -g wheel -m 600 shadow ${DESTDIR}/etc
#	${INSTALL} -c -o root -g wheel -m 600 master.passwd ${DESTDIR}/etc
#	${INSTALL} -c -o ${OWN} -g ${GRP} -m 555 MAKEDEV.local MAKEDEV \
#	    ${DESTDIR}/dev
	${INSTALL} -c -o ${OWN} -g ${GRP} -m 555 MAKEDEV \
	    ${DESTDIR}/etc
	${INSTALL} -c -o root -g wheel -m 644 root/dot.profile \
	    ${DESTDIR}/root/.profile
	${INSTALL} -c -o root -g wheel -m 644 root/dot.profile \
	    ${DESTDIR}/.profile
	${INSTALL} -d -m 755 ${DESTDIR}/home/operator
	${INSTALL} -c -m 644 operator/dot.profile \
	    ${DESTDIR}/home/operator/.profile
	ln -sf ${TZDIR}/${LOCALTIME} ${DESTDIR}/etc/localtime
	${INSTALL} -c -m 444 ${DESTDIR}/usr/share/misc/termcap \
	    ${DESTDIR}/etc/termcap
#	${INSTALL} -c -o ${OWN} -g operator -m 664 /dev/null \
#	    ${DESTDIR}/etc/dumpdates
	${INSTALL} -d -o ${OWN} -g ${GRP} -m 655 ${DESTDIR}/var/run
	${INSTALL} -c -o ${OWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/messages
#	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 /dev/null \
#	    ${DESTDIR}/usr/adm/daemonlog
#	${INSTALL} -c -o ${OWN} -g operator -m 664 /dev/null \
#	    ${DESTDIR}/usr/adm/shutdownlog
#	${INSTALL} -c -o ${OWN} -g ${GRP} -m 644 /dev/null \
#	    ${DESTDIR}/var/run/utmp
#	${INSTALL} -c -o nobody -g ${GRP} -m 644 /dev/null \
#	    ${DESTDIR}/var/db/find.codes
#	${INSTALL} -c -o ${OWN} -g wheel -m 644 /dev/null \
#	    ${DESTDIR}/usr/adm/lastlog
	${INSTALL} -c -o ${OWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/wtmp
#	${INSTALL} -c -o uucp -g daemon -m 600 /dev/null \
#	    ${DESTDIR}/usr/adm/aculog

DIRS_DISTRIB=	bin sbin dev etc var/log var/run root usr usr/bin usr/sbin \
		usr/include usr/lib usr/libexec usr/share usr/games/lib
MDIRS_DISTRIB=	cat1 cat2 cat3 cat4 cat5 cat6 cat7 cat8 cat9

# XXX Keep in sync with SUBDIR in sys/arch/${MACHINE}/compile/Makefile
DIRS_RELEASE!=if [ x"${MACHINE}" = x"stm32" ] ; then \
		echo "F405WEACTCORE F411RENUCLEO F412GDISCO F412WEACTCORE \
			F413HDISCO F446RENUCLEO F446WEACTCORE F469IDISCO \
			F4DISCOVERY F4VEDEVEBOX" ; \
	elif [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "PICO" ; \
	else \
		echo "FAIL" ; \
	fi

# Creates an empty DiscoBSD directory tree in DESTDIR.
distrib-dirs:
	if [ ! -d ${DESTDIR}/. ]; then \
		${INSTALL} -d -m 755 ${DESTDIR}; \
	fi
	for dir in ${DIRS_DISTRIB}; do \
		mkdir -p ${DESTDIR}/$$dir; \
	done
	for dir in ${MDIRS_DISTRIB}; do \
		mkdir -p ${DESTDIR}/usr/share/man/$$dir; \
	done

# Creates and populates a DiscoBSD release directory tree in RELEASEDIR.
release-dirs:
	if [ ! -d ${RELEASEDIR}/. ]; then \
		${INSTALL} -d -m 755 ${RELEASEDIR}; \
	fi
	${INSTALL} -d -m 755 ${RELEASEDIR}/${MACHINE}
	cd ${RELEASEDIR}/${MACHINE} && \
	for dir in ${DIRS_RELEASE}; do \
		mkdir -p $$dir; \
		cp ${TOPSRC}/sys/arch/${MACHINE}/compile/$$dir/unix* $$dir; \
	done
	cp -f ${TOPSRC}/distrib/${MACHINE}/sdcard.img ${RELEASEDIR}/${MACHINE}
	cp -f ${TOPSRC}/distrib/${MACHINE}/README.md ${RELEASEDIR}/${MACHINE}
	cp -f ${TOPSRC}/distrib/notes/ANNOUNCEMENT.md ${RELEASEDIR}
	cp -f ${TOPSRC}/README.md ${RELEASEDIR}

release: release-dirs
	cd ${RELEASEDIR} && \
	tar czvf DiscoBSD_${OSRev}_${MACHINE}.tar.gz ${MACHINE} && \
	zip -r DiscoBSD_${OSRev}_${MACHINE}.zip ${MACHINE}

.PHONY:	all distribution distribution-etc-root-var \
	distrib-dirs release-dirs release clean
