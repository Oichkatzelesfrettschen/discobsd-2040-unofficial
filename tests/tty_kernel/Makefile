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

# Host gate for the kernel's VMIN and VTIME storage and its drain request: the
# production sys/kern/tty.c code is extracted (extract.py) and compiled against
# the kernel's own headers at ILP32, as tests/libc_sysctl does for libc.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
PROG=	tty_kernel_test
SHIM_DIR=	${TOPSRC}/tests/libc_sysctl
ILP32=	-m32
MACHINE_STAMP=	machine/.stamp
PORT_INCLUDE=	${TOPSRC}/sys/arch/${MACHINE}/include
WARN=	-Wall -Wextra -Werror
KERNEL_FLAGS=	-std=gnu17 ${WARN} ${ILP32} -ffreestanding -fno-builtin \
	-nostdinc -I. -I${SHIM_DIR} -I${TOPSRC}/sys -I${TOPSRC}/sys/arch \
	-I${PORT_INCLUDE}
EXTRACTED=	tty_extracted.c
JOB_CONTROL_REQUESTS=	TIOCSMINTIME TIOCDRAIN TIOCSTOP TIOCSTART

ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

check:
.if ${ILP32_OK} == "yes"
	@${MAKE} ${PROG}
	${TEST_EXECUTION} run tty-kernel.ilp32 -- ./${PROG}
	@${MAKE} mutants
.else
	${TEST_EXECUTION} skip tty-kernel.ilp32 "tty_kernel: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

# The kernel headers reach <machine/*.h>; one forwarding header per port header.
${MACHINE_STAMP}:
	@mkdir -p machine
	@for header in ${PORT_INCLUDE}/*.h; do \
		name=`basename $$header`; \
		echo "#include <${MACHINE}/include/$$name>" > machine/$$name; \
	done
	@touch $@

${EXTRACTED}: extract.py ${TOPSRC}/sys/kern/tty.c ${TOPSRC}/sys/kern/sys_generic.c
	${PYTHON} extract.py --source ${TOPSRC}/sys/kern/tty.c \
	    --payload-source ${TOPSRC}/sys/kern/sys_generic.c --output $@

shim.o: ${SHIM_DIR}/libc_shim.c ${SHIM_DIR}/libc_shim.h
	${HOSTCC} -std=c99 ${WARN} ${ILP32} -c -o $@ ${SHIM_DIR}/libc_shim.c

gate.o: tty_kernel_test.c ${EXTRACTED} ${MACHINE_STAMP}
	${HOSTCC} ${KERNEL_FLAGS} -c -o $@ tty_kernel_test.c

${PROG}: gate.o shim.o
	${HOSTCC} ${ILP32} -o $@ gate.o shim.o

# Each line of mutants.tsv is a sed script over the extracted source that the
# gate must reject; a script that leaves the source unchanged or breaks the build is an
# error.
mutants: ${EXTRACTED} shim.o ${MACHINE_STAMP}
	@for label in ${JOB_CONTROL_REQUESTS}; do \
		sed "0,/^    case $$label:\$$/{//d;}" ${TOPSRC}/sys/kern/tty.c > tty_nolist.c; \
		if cmp -s tty_nolist.c ${TOPSRC}/sys/kern/tty.c; then \
			echo "FAIL job-control $$label: the label was not found" >&2; exit 1; fi; \
		if ${PYTHON} extract.py --source tty_nolist.c \
		    --payload-source ${TOPSRC}/sys/kern/sys_generic.c \
		    --output tty_nolist_out.c 2>/dev/null; then \
			echo "FAIL job-control $$label dropped from the list survived" >&2; exit 1; fi; \
		echo "PASS job-control list without $$label rejected"; \
	done; rm -f tty_nolist.c tty_nolist_out.c
	@while IFS='	' read -r label script; do \
		case "$$label" in ''|'#'*) continue;; esac; \
		HOSTCC="${HOSTCC}" KERNEL_FLAGS="${KERNEL_FLAGS}" EXTRACTED="${EXTRACTED}" sh ./mutant.sh "$$label" "$$script" || exit 1; \
	done < mutants.tsv

clean:
	rm -f ${PROG} gate.o shim.o ${EXTRACTED}
	rm -f machine/*.h ${MACHINE_STAMP}
	@if [ -d machine ]; then rmdir machine; fi

all: check

.PHONY: all check clean mutants
