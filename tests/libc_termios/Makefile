# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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

# Host gate for the termios library members of lib/libc/gen, compiled
# against the tree's own userland headers and run at ILP32 as
# tests/libc_sysctl does; that Makefile states why.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
PROG=	libc_termios_test
TARGET_DIR=	${TOPSRC}/lib/libc/gen
SHIM_DIR=	${TOPSRC}/tests/libc_sysctl

ILP32=	-m32
MACHINE_STAMP=	machine/.stamp
PORT_INCLUDE=	${TOPSRC}/include/${MACHINE}
TREE=	-I. -I${SHIM_DIR} -I${TOPSRC}/include

WARN=	-Wall -Wextra -Werror
CFLAGS=	-std=c99 ${WARN} -fno-builtin ${ILP32}
RENAMES=	-Dioctl=test_ioctl -Derrno=test_errno

MEMBERS=	cfspeed.o cfmakeraw.o tcattr.o tcdrain.o tcflow.o tcflush.o
ORDER=	order_termios.o order_ioctl.o

ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG}
	${TEST_EXECUTION} run libc-termios.ilp32 -- ./${PROG}
	@${MAKE} mutants
.else
	${TEST_EXECUTION} skip libc-termios.ilp32 "libc_termios: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

${MACHINE_STAMP}:
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/$$name>" > machine/$$name; \
	done
	@touch $@

shim.o: ${SHIM_DIR}/libc_shim.c ${SHIM_DIR}/libc_shim.h
	${HOSTCC} -std=c99 ${WARN} ${ILP32} -c -o $@ ${SHIM_DIR}/libc_shim.c

.for member in ${MEMBERS:R}
${member}.o: ${TARGET_DIR}/${member}.c ${MACHINE_STAMP} ${TOPSRC}/include/termios.h
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} -c -o $@ ${TARGET_DIR}/${member}.c
.endfor

gate.o: libc_termios_test.c termios_model.h ${MACHINE_STAMP} ${TOPSRC}/include/termios.h
	${HOSTCC} ${CFLAGS} ${TREE} ${RENAMES} -c -o $@ libc_termios_test.c

order_termios.o: order.c ${MACHINE_STAMP} ${TOPSRC}/include/termios.h
	${HOSTCC} ${CFLAGS} ${TREE} -c -o $@ order.c

order_ioctl.o: order.c ${MACHINE_STAMP} ${TOPSRC}/include/termios.h
	${HOSTCC} ${CFLAGS} ${TREE} -DORDER_IOCTL_FIRST -c -o $@ order.c

${PROG}: gate.o ${MEMBERS} ${ORDER} shim.o
	${HOSTCC} ${CFLAGS} -o $@ gate.o ${MEMBERS} ${ORDER} shim.o

# Every line of mutants.tsv is one change to a shipped source that the gate
# must reject; mutant.sh builds it apart from the tree.
mutants: ${PROG}
	@while IFS='	' read -r label file script; do \
		case "$$label" in ''|'#'*) continue;; esac; \
		HOSTCC='${HOSTCC}' TOPSRC='${TOPSRC}' MEMBERS='${MEMBERS}' \
		    BUILD_FLAGS='${CFLAGS} ${TREE} ${RENAMES}' \
		    EXTRA_OBJECTS='${ORDER} shim.o' \
		    sh ./mutant.sh "$$label" "$$file" "$$script" || exit 1; \
	done < mutants.tsv

clean:
	rm -f ${PROG} gate.o ${MEMBERS} ${ORDER} shim.o
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

all: check

.PHONY: all check clean mutants
