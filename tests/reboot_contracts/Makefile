#
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Host contract of sbin/reboot/reboot.c: the arming rules of the boot ROM
# loader and the unchanged flags of the other modes. "check" compiles the
# shipped source under the system call recorders in reboot_contract_test.c;
# "mutants" compiles broken copies that the same harness must reject.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
HOST_CFLAGS=	-std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Werror \
		-Wno-unused-parameter -Wno-deprecated-non-prototype -I${.CURDIR}/shim
REBOOT_RENAMES=	-Dmain=discobsd_reboot_main -Dreboot=test_reboot \
		-Dkill=test_kill -Dsync=test_sync -Dsleep=test_sleep \
		-Dgeteuid=test_geteuid -Dopen=test_open \
		-Dopenlog=test_openlog -Dsyslog=test_syslog \
		-Dgetlogin=test_getlogin

check: reboot_contract_test
	./reboot_contract_test
	sh ${.CURDIR}/boardinfo_test.sh ${TOPSRC}/usr.bin/boardinfo/boardinfo ${.OBJDIR}

reboot_contract_test: reboot_contract_test.c reboot_source.o
	${HOSTCC} ${HOST_CFLAGS} -o $@ reboot_contract_test.c reboot_source.o

reboot_source.o: ${TOPSRC}/sbin/reboot/reboot.c
	${HOSTCC} ${HOST_CFLAGS} ${REBOOT_RENAMES} -c -o $@ \
	    ${TOPSRC}/sbin/reboot/reboot.c

# The shim directory shadows only sys/reboot.h and utmp.h, and its two
# fallbacks (_PATH_WTMP, kill) are calibrated by removing each.
sdk-gap:
	sh ${.CURDIR}/sdk_gap.sh "${HOSTCC}" "${HOST_CFLAGS}" ${.CURDIR}

# Each mutant must fail the harness: no root check, no confirmation check,
# confirmation always set, and a lost -y or -B option.
mutants: sdk-gap
	sh ${.CURDIR}/mutants.sh "${HOSTCC}" "${HOST_CFLAGS}" \
	    "${REBOOT_RENAMES}" ${TOPSRC}/sbin/reboot/reboot.c ${.CURDIR}

clean:
	rm -f reboot_contract_test reboot_source.o mutant_* bi-*.out bi-mutant.sh bi-mutant2.sh bi-id*
	rm -rf boardinfo-stubs

all: check mutants

.PHONY: all check mutants clean
