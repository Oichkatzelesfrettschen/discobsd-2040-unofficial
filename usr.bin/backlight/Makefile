# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# backlight: show and set the PicoCalc's screen and keyboard backlights
# (backlight.1). distrib/rp2040/mi.rp2040 installs it setuid root in the
# backlight closure, which the PicoCalc profiles name.
#
#   all     the target program and its manual page
#   host    backlight-host: backlight.c over the controller model of
#           tests/bl_fixture.c
#   test    the host cases of tests/check.py, then its mutants
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		backlight
OBJS=		${PROG}.o bl_sysctl.o
CFLAGS		+= -std=c17 -pedantic-errors -Wall -Wextra -Werror
MAN		= backlight.0
MANSRC		= backlight.1

all:		${PROG} ${MAN}

${OBJS}:	backlight.h

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${MAN}:	${MANSRC}
		${MANROFF} ${MANSRC} > ${MAN}

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

HOSTCC?=	cc

test:		require-python
		HOSTCC=${HOSTCC} ${PYTHON} -I -u tests/check.py .

clean:
		rm -f *.o *.elf ${MAN} ${PROG} tags *~
		rm -rf tests/__pycache__

install:	all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
