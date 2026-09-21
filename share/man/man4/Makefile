#
# Copyright (c) 1987 Regents of the University of California.
# All rights reserved.
#
# Redistribution and use in source and binary forms are permitted
# provided that the above copyright notice and this paragraph are
# duplicated in all such forms and that any documentation,
# advertising materials, and other materials related to such
# distribution and use acknowledge that the software was developed
# by the University of California, Berkeley.  The name of the
# University may not be used to endorse or promote products derived
# from this software without specific prior written permission.
# THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
# WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
#
#	@(#)Makefile	5.4.3 (2.11BSD) 1997/5/31
#
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

MDIR=	${DESTDIR}/usr/share/man/cat4
SRCS=	arp.4 bk.4 fd.4 icmp.4 idp.4 inet.4 ip.4 lo.4 mem.4 mtio.4 \
	networking.4 ns.4 nsip.4 null.4 pty.4 ram.4 spp.4 swap.4 tb.4 \
	tcp.4 tty.4 udp.4
OBJS=	arp.0 bk.0 fd.0 icmp.0 idp.0 inet.0 ip.0 lo.0 mem.0 mtio.0 \
	networking.0 ns.0 nsip.0 null.0 pty.0 ram.0 spp.0 swap.0 tb.0 \
	tcp.0 tty.0 udp.0

.SUFFIXES: .4 .0

.4.0:
	${MANROFF} $*.4 > $*.0

all: ${OBJS}

clean: FRC
	rm -f ${OBJS}

install: all
	cp ${OBJS} ${MDIR}
	ln -f ${MDIR}/mem.0 ${MDIR}/kmem.0
	ln -f ${MDIR}/fd.0 ${MDIR}/stdin.0
	ln -f ${MDIR}/fd.0 ${MDIR}/stderr.0
	ln -f ${MDIR}/fd.0 ${MDIR}/stdout.0

FRC:
