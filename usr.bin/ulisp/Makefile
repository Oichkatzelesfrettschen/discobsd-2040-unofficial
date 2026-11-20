# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		ulisp
BINDIR=		/usr/bin
SHAREDIR=	/usr/share/ulisp
OBJS=		ulisp.o unix.o hilite.o gfx.o dev_picocalc.o font.o
FONTDIR=	${TOPSRC}/sys/arch/rp2040/dev
CFLAGS+=	-DULISP_BOARD -DULISP_SGTTY -I${FONTDIR}
LIBS=		-lm -lc

HOST_SRCS=	ulisp.c unix.c hilite.c gfx.c dev_host.c ${FONTDIR}/lcdfont.c
HOST_CFLAGS=	-std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Os \
		-I${FONTDIR}

all: ${PROG} ulisp.doc

ulisp.o: ulisp.c builtins.def platform.h
hilite.o unix.o: platform.h

font.o: ${FONTDIR}/lcdfont.c
	${CC} ${CFLAGS} -c -o $@ ${FONTDIR}/lcdfont.c

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -Wl,-Map=$@.map -o $@.elf ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@

# The documentation strings stay out of the program, whose every byte is
# resident in the process window; mkdoc writes them to the file that
# (documentation) and (?) read.
mkdoc: mkdoc.c builtins.def
	${HOST_CC} -std=c17 -Wall -Wextra -Werror -o $@ mkdoc.c

ulisp.doc: mkdoc
	./mkdoc $@

ulisp.host: ${HOST_SRCS} builtins.def platform.h
	${HOST_CC} ${HOST_CFLAGS} -o $@ ${HOST_SRCS} -lm

install: all
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	mkdir -p ${DESTDIR}${SHAREDIR}
	rm -f ${DESTDIR}${SHAREDIR}/ulisp.doc
	cp ulisp.doc ${DESTDIR}${SHAREDIR}/ulisp.doc
	chmod 444 ${DESTDIR}${SHAREDIR}/ulisp.doc

clean:
	rm -f ${PROG} ${PROG}.elf ${PROG}.map ${PROG}.host ${PROG}.sites mkdoc \
	    ulisp.doc \
	    *.o *.dis

.PHONY: all install clean
