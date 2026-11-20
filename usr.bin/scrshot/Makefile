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
# scrshot: save what the PicoCalc panel shows to a file (scrshot.1). It
# reads /dev/lcdgfx and shows the console only to the superuser, on a
# PICOCALC_SD kernel; it
# ships on the PicoCalc SD root only, so mi.rp2040 does not name it.
#
#   all     the target program and its manual page
#   test    the host test of the format writers (scrfmt.h), then its mutants
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Wextra -Werror

SRCS		= scrshot.c
OBJS		= scrshot.o
MAN		= scrshot.0
MANSRC		= scrshot.1

all:		scrshot scrshot.0

${OBJS}:	scrfmt.h

scrshot:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o scrshot.elf ${OBJS} ${LIBS}
		${SIZE} scrshot.elf
		${ELF2AOUT} scrshot.elf $@ && rm scrshot.elf

scrshot.0:	${MANSRC}
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
		rm -f *.o *.elf ${MAN} scrshot tags *~
		rm -rf tests/__pycache__

install:	all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} scrshot ${DESTDIR}/usr/bin/scrshot

.PHONY:		all test require-python clean install
