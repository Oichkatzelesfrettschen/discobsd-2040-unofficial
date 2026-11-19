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
# word -- the clean-room word processor of SPEC.md. The rp2040 build is
# installed to /usr/bin/word, which the PicoCalc SD root carries
# (distrib/rp2040/mksdmanifest.py); the flash root manifest leaves it out.
#
#   all     the target program
#   host    word.host, the same sources over termios
#   test    the PTY tests of tests/word_test.py, as a native and an ILP32
#           program, then the mutants
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		word
SRCS=		word.c word_buf.c word_find.c word_menus.c word_modes.c \
		word_pick.c word_term.c
OBJS=		${SRCS:.c=.o}
CFLAGS		+= -std=c17

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	word.h

HOSTCC?=	cc
HOST_CFLAGS=	-std=c17 -D_DEFAULT_SOURCE -DTERMIOS -Wall -Wextra -Werror \
		-Wshadow -O1

host:		word.host

word.host:	${SRCS} word.h
		${HOSTCC} ${HOST_CFLAGS} -o $@ ${SRCS}

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test-pty:	require-python host
		${PYTHON} -I -u tests/word_test.py ./word.host

# The same tests as an ILP32 program: long and size_t are 32 bits there,
# as on the target.
M32_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh ilp32 ${HOSTCC} -m32

test-pty32:	require-python
.if ${M32_OK} == "yes"
		${HOSTCC} ${HOST_CFLAGS} -m32 -o word.host32 ${SRCS}
		${PYTHON} -I -u tests/word_test.py ./word.host32
.else
		@echo "SKIP word ILP32: ${HOSTCC} -m32 does not link and run; no 32-bit run occurred"
.endif

test-mutants:	require-python
		HOSTCC=${HOSTCC} ${PYTHON} -I -u tests/mutants.py .

test:		test-pty test-pty32 .WAIT test-mutants

clean:
		rm -f *.o *.elf *.dis ${PROG} word.host word.host32
		rm -rf tests/__pycache__

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		all host test test-pty test-pty32 test-mutants require-python clean install
