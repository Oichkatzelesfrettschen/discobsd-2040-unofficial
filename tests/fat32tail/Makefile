TOPSRC!=cd ../..; pwd

# The tools/hsaout library tools/fsutil links, as tests/fsutil_clock builds it.
include ${TOPSRC}/share/mk/sys.mk

PYTHON?=
HSAOUT=		${TOPSRC}/tools/hsaout

check:
		${MAKE} -C ${HSAOUT} MACHINE=${MACHINE} libhsaout.a
		MAKE=${MAKE:Q} PYTHON=${PYTHON:Q} sh ${.CURDIR}/check.sh ${HSAOUT}/libhsaout.a

.PHONY: check
