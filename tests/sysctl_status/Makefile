TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
HOSTCC?= cc
ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

check: check.sh check.py fixture.c shim.c check_workload.py
.if ${ILP32_OK} == "yes"
	PYTHON=${PYTHON:Q} HOSTCC=${HOSTCC:Q} ${TEST_EXECUTION} run sysctl.status.ilp32 -- sh ./check.sh
.else
	${TEST_EXECUTION} skip sysctl.status.ilp32 "sysctl status: ILP32 runtime unavailable"
.endif
	${PYTHON} check_workload.py ${TOPSRC} /bin/sh

.PHONY: check
